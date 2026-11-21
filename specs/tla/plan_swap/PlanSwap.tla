---------------------------- MODULE PlanSwap ----------------------------
(* The native engine's plan publish and the retirement of the epoch it replaces.
   Models EngineSession::publish/process (magda/engine/exec/EngineSession.cpp),
   farbot NonRealtimeMutatable (third_party/farbot/.../RealtimeObject.tcc),
   RenderAhead::setEpoch, and RenderThreadPool render/takeWork/release with
   ParallelPlanExecutor::letGoOfPool. Each epoch has two jobs, one per pool slot (#3360):
   the callback's, rendered by the audio thread, and the one the render-ahead thread renders,
   which the audio thread helps finish (PlanExecutor::helpWriter). Whichever thread finishes
   an ahead block's last op renders its tail and publishes it, and its caller waits for that
   (Schedule::finishAheadBlock, #3366).
   One label per seq_cst atomic operation; everything between labels is one step. *)
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Workers, MaxEpoch, MaxBlocks, MaxAheadBlocks, Tasks

NULL == 0
Epochs == 1..MaxEpoch
Slots == 1..2                           \* RenderThreadPool::Slot: callback, ahead
Jobs == 1..(2 * MaxEpoch)
JobOf(e, s) == 2 * (e - 1) + s
EpochOf(j) == (j + 1) \div 2

(* --algorithm PlanSwap
variables
    pointer = 1,                        \* farbot `pointer`; NULL while the audio thread holds it
    storage = 1,                        \* farbot `storage`, publisher-owned
    aheadEpoch = 1,                     \* RenderAhead::epoch_, under its lock
    aheadBusy = FALSE,                  \* a renderOnce() is holding RenderAhead's epoch
    live = 1,                           \* EngineSession::live_
    nextEpoch = 2,
    freed = {},                         \* epochs whose PreparedRender has been destroyed
    jobPtr = [s \in Slots |-> NULL],    \* RenderThreadPool::jobs_, never cleared at block end
    queued = 0,                         \* RenderThreadPool::queued_
    handed = [j \in Jobs |-> FALSE],    \* ParallelPlanExecutor::handedToPool_
    ready = [j \in Jobs |-> 0],         \* ops in the executor's ready_ queue
    remaining = [j \in Jobs |-> 0],     \* ParallelPlanExecutor::remaining_
    inside = [j \in Jobs |-> 0],        \* Job::workersInside
    finishes = [j \in Jobs |-> 0],      \* finishAheadBlock calls this block, ghost
    published = [j \in Jobs |-> FALSE], \* Schedule::finished_
    arrival = [w \in Workers |-> 0];    \* Worker::arrival, mod 4: odd and the arrival it names

define
    Touchable(j) == EpochOf(j) \notin freed
    IsAhead(j) == j % 2 = 0
    Retired == { e \in Epochs : e < live }
end define;

\* EngineSession::publish, message thread. The old epoch dies at `live_ = ...`.
fair+ process Publisher = "message"
variables old = NULL, rel = NULL, slotRel = 1, pending = {}, marked = 0;
begin
Publish:
    while nextEpoch <= MaxEpoch do
    Swap:       \* nonRealtimeReplace: CAS spins until the audio thread has released
        await pointer = storage;
        pointer := nextEpoch;
        storage := nextEpoch;
    SetAhead:   \* RenderAhead::setEpoch: waits out a render in progress
        await ~aheadBusy;
        aheadEpoch := nextEpoch;
    Retire:     \* live_ = std::move(prepared): ~ParallelPlanExecutor -> letGoOfPool, each job
        old := live;
        live := nextEpoch;
        nextEpoch := nextEpoch + 1;
        slotRel := 1;
    ReleaseJob:
        while slotRel <= 2 do
            rel := JobOf(old, slotRel);
            slotRel := slotRel + 1;
            if ~handed[rel] then goto ReleaseJob; end if;
    ReleaseCas: \* release(): compare_exchange_strong(&job, nullptr) on every slot
            jobPtr := [s \in Slots |-> IF jobPtr[s] = rel THEN NULL ELSE jobPtr[s]];
            pending := Workers;
    ReleaseMarks:
            while pending /= {} do
                with x \in pending do
                    marked := arrival[x];
                    pending := pending \ {x};
                    if marked % 2 = 1 then
                        await arrival[x] /= marked;
                    end if;
                end with;
            end while;
    ReleaseInside:
            await inside[rel] = 0;
        end while;
    Free:
        freed := freed \union {old};
    end while;
end process;

\* EngineSession::process, one acquire per piece of a driver callback.
fair process Audio = "audio"
variables cur = NULL, blocks = 0, helping = NULL, helpLast = FALSE;
begin
Piece:
    while blocks < MaxBlocks do
    Acquire:    \* realtimeAcquire: pointer.exchange(nullptr)
        cur := JobOf(pointer, 1);
        pointer := NULL;
        blocks := blocks + 1;
    Help:       \* helpAheadSide: an op of its epoch's ahead job, taken as a worker takes one
        either
            skip;
        or
            helping := JobOf(EpochOf(cur), 2);
            assert Touchable(helping);
            if ready[helping] > 0 then
                ready[helping] := ready[helping] - 1;
                queued := queued - 1;
    HelpRun:
                assert Touchable(helping);
                helpLast := remaining[helping] = 1;
                remaining[helping] := remaining[helping] - 1;
    HelpFinishBlock:
                if helpLast then
                    assert Touchable(helping) /\ finishes[helping] = 0;
                    finishes[helping] := 1;
    HelpPublished:
                    published[helping] := TRUE;
                end if;
            end if;
        end either;
    Render:
        assert Touchable(cur);
        either
            skip;               \* renderInPlanOrder: the pool is not used
        or
            ready[cur] := Tasks || remaining[cur] := Tasks;     \* startSchedule
            queued := queued + Tasks;
            handed[cur] := TRUE;
    StoreJob:   \* render(): jobs_[callback].store(&job)
            jobPtr[1] := cur;
    Finish:     \* finishOnCaller: take work until remaining_ reaches zero
            while remaining[cur] > 0 do
                assert Touchable(cur);
                if ready[cur] > 0 then
                    ready[cur] := ready[cur] - 1;
                    queued := queued - 1;
    CallerRun:
                    remaining[cur] := remaining[cur] - 1;
                end if;
            end while;
        end either;
    Release:    \* realtimeRelease: pointer.store(currentObj)
        pointer := EpochOf(cur);
    end while;
end process;

\* RenderAhead::renderOnce, holding the epoch it was handed for the whole render.
fair process Ahead = "ahead"
variables acur = NULL, aheadBlocks = 0, aheadLast = FALSE;
begin
AheadPiece:
    while aheadBlocks < MaxAheadBlocks do
    AheadAcquire:
        await ~aheadBusy;
        aheadBusy := TRUE;
        acur := JobOf(aheadEpoch, 2);
        aheadBlocks := aheadBlocks + 1;
    AheadRender:
        assert Touchable(acur);
        either
            skip;
        or
            \* The block before this one is published: the job is reused for this one.
            assert ~handed[acur] \/ published[acur];
            ready[acur] := Tasks || remaining[acur] := Tasks;
            finishes[acur] := 0;
            published[acur] := FALSE;
            queued := queued + Tasks;
            handed[acur] := TRUE;
    AheadStoreJob:  \* render(): jobs_[ahead].store(&job)
            jobPtr[2] := acur;
    AheadFinish:
            while remaining[acur] > 0 do
                assert Touchable(acur);
                if ready[acur] > 0 then
                    ready[acur] := ready[acur] - 1;
                    queued := queued - 1;
    AheadCallerRun:
                    aheadLast := remaining[acur] = 1;
                    remaining[acur] := remaining[acur] - 1;
    AheadFinishBlock:
                    if aheadLast then
                        assert Touchable(acur) /\ finishes[acur] = 0;
                        finishes[acur] := 1;
    AheadPublished:
                        published[acur] := TRUE;
                    end if;
                end if;
            end while;
    AheadAwait:     \* finishOnCaller: the block's last op may have finished on another thread
            await published[acur];
            assert finishes[acur] = 1;
        end either;
    AheadRelease:
        aheadBusy := FALSE;
    end while;
end process;

\* RenderThreadPool::Worker::run -> takeWork, the callback's slot first, then takeFrom per slot
fair process Worker \in Workers
variables job = NULL, took = FALSE, slot = 1, last = FALSE;
begin
Wait:
    while TRUE do
        await queued > 0;
        slot := 1;
    TrySlot:
        while slot <= 2 do
        Arrive:
            arrival[self] := (arrival[self] + 1) % 4;
        LoadJob:
            job := jobPtr[slot];
        CountIn:
            if job /= NULL then
                assert Touchable(job);
                inside[job] := inside[job] + 1;
            end if;
        Arrived:
            arrival[self] := (arrival[self] + 1) % 4;
            if job = NULL then
                slot := slot + 1;
                goto TrySlot;
            end if;
        TakeOne:    \* ready_->try_pop
            assert Touchable(job);
            if ready[job] > 0 then
                ready[job] := ready[job] - 1;
                queued := queued - 1;
                took := TRUE;
            else
                took := FALSE;
            end if;
        RunChain:   \* remaining_.fetch_sub, the chain's last touch of the block
            if took then
                assert Touchable(job);
                last := IsAhead(job) /\ remaining[job] = 1;
                remaining[job] := remaining[job] - 1;
            end if;
        WorkerFinishBlock:  \* finishAheadBlock, still counted inside the job
            if took /\ last then
                assert Touchable(job) /\ finishes[job] = 0;
                finishes[job] := 1;
        WorkerPublished:
                published[job] := TRUE;
            end if;
        CountOut:
            assert Touchable(job);
            inside[job] := inside[job] - 1;
            if took then
                goto Wait;
            else
                slot := slot + 1;
            end if;
        end while;
    end while;
end process;
end algorithm; *)
\* BEGIN TRANSLATION (chksum(pcal) = "cf4014a" /\ chksum(tla) = "8c9b3a1b")
VARIABLES pointer, storage, aheadEpoch, aheadBusy, live, nextEpoch, freed,
          jobPtr, queued, handed, ready, remaining, inside, finishes,
          published, arrival, pc

(* define statement *)
Touchable(j) == EpochOf(j) \notin freed
IsAhead(j) == j % 2 = 0
Retired == { e \in Epochs : e < live }

VARIABLES old, rel, slotRel, pending, marked, cur, blocks, helping, helpLast,
          acur, aheadBlocks, aheadLast, job, took, slot, last

vars == << pointer, storage, aheadEpoch, aheadBusy, live, nextEpoch, freed,
           jobPtr, queued, handed, ready, remaining, inside, finishes,
           published, arrival, pc, old, rel, slotRel, pending, marked, cur,
           blocks, helping, helpLast, acur, aheadBlocks, aheadLast, job, took,
           slot, last >>

ProcSet == {"message"} \cup {"audio"} \cup {"ahead"} \cup (Workers)

Init == (* Global variables *)
        /\ pointer = 1
        /\ storage = 1
        /\ aheadEpoch = 1
        /\ aheadBusy = FALSE
        /\ live = 1
        /\ nextEpoch = 2
        /\ freed = {}
        /\ jobPtr = [s \in Slots |-> NULL]
        /\ queued = 0
        /\ handed = [j \in Jobs |-> FALSE]
        /\ ready = [j \in Jobs |-> 0]
        /\ remaining = [j \in Jobs |-> 0]
        /\ inside = [j \in Jobs |-> 0]
        /\ finishes = [j \in Jobs |-> 0]
        /\ published = [j \in Jobs |-> FALSE]
        /\ arrival = [w \in Workers |-> 0]
        (* Process Publisher *)
        /\ old = NULL
        /\ rel = NULL
        /\ slotRel = 1
        /\ pending = {}
        /\ marked = 0
        (* Process Audio *)
        /\ cur = NULL
        /\ blocks = 0
        /\ helping = NULL
        /\ helpLast = FALSE
        (* Process Ahead *)
        /\ acur = NULL
        /\ aheadBlocks = 0
        /\ aheadLast = FALSE
        (* Process Worker *)
        /\ job = [self \in Workers |-> NULL]
        /\ took = [self \in Workers |-> FALSE]
        /\ slot = [self \in Workers |-> 1]
        /\ last = [self \in Workers |-> FALSE]
        /\ pc = [self \in ProcSet |-> CASE self = "message" -> "Publish"
                                        [] self = "audio" -> "Piece"
                                        [] self = "ahead" -> "AheadPiece"
                                        [] self \in Workers -> "Wait"]

Publish == /\ pc["message"] = "Publish"
           /\ IF nextEpoch <= MaxEpoch
                 THEN /\ pc' = [pc EXCEPT !["message"] = "Swap"]
                 ELSE /\ pc' = [pc EXCEPT !["message"] = "Done"]
           /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                           nextEpoch, freed, jobPtr, queued, handed, ready,
                           remaining, inside, finishes, published, arrival,
                           old, rel, slotRel, pending, marked, cur, blocks,
                           helping, helpLast, acur, aheadBlocks, aheadLast,
                           job, took, slot, last >>

Swap == /\ pc["message"] = "Swap"
        /\ pointer = storage
        /\ pointer' = nextEpoch
        /\ storage' = nextEpoch
        /\ pc' = [pc EXCEPT !["message"] = "SetAhead"]
        /\ UNCHANGED << aheadEpoch, aheadBusy, live, nextEpoch, freed, jobPtr,
                        queued, handed, ready, remaining, inside, finishes,
                        published, arrival, old, rel, slotRel, pending, marked,
                        cur, blocks, helping, helpLast, acur, aheadBlocks,
                        aheadLast, job, took, slot, last >>

SetAhead == /\ pc["message"] = "SetAhead"
            /\ ~aheadBusy
            /\ aheadEpoch' = nextEpoch
            /\ pc' = [pc EXCEPT !["message"] = "Retire"]
            /\ UNCHANGED << pointer, storage, aheadBusy, live, nextEpoch,
                            freed, jobPtr, queued, handed, ready, remaining,
                            inside, finishes, published, arrival, old, rel,
                            slotRel, pending, marked, cur, blocks, helping,
                            helpLast, acur, aheadBlocks, aheadLast, job, took,
                            slot, last >>

Retire == /\ pc["message"] = "Retire"
          /\ old' = live
          /\ live' = nextEpoch
          /\ nextEpoch' = nextEpoch + 1
          /\ slotRel' = 1
          /\ pc' = [pc EXCEPT !["message"] = "ReleaseJob"]
          /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, freed,
                          jobPtr, queued, handed, ready, remaining, inside,
                          finishes, published, arrival, rel, pending, marked,
                          cur, blocks, helping, helpLast, acur, aheadBlocks,
                          aheadLast, job, took, slot, last >>

ReleaseJob == /\ pc["message"] = "ReleaseJob"
              /\ IF slotRel <= 2
                    THEN /\ rel' = JobOf(old, slotRel)
                         /\ slotRel' = slotRel + 1
                         /\ IF ~handed[rel']
                               THEN /\ pc' = [pc EXCEPT !["message"] = "ReleaseJob"]
                               ELSE /\ pc' = [pc EXCEPT !["message"] = "ReleaseCas"]
                    ELSE /\ pc' = [pc EXCEPT !["message"] = "Free"]
                         /\ UNCHANGED << rel, slotRel >>
              /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                              nextEpoch, freed, jobPtr, queued, handed, ready,
                              remaining, inside, finishes, published, arrival,
                              old, pending, marked, cur, blocks, helping,
                              helpLast, acur, aheadBlocks, aheadLast, job,
                              took, slot, last >>

ReleaseCas == /\ pc["message"] = "ReleaseCas"
              /\ jobPtr' = [s \in Slots |-> IF jobPtr[s] = rel THEN NULL ELSE jobPtr[s]]
              /\ pending' = Workers
              /\ pc' = [pc EXCEPT !["message"] = "ReleaseMarks"]
              /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                              nextEpoch, freed, queued, handed, ready,
                              remaining, inside, finishes, published, arrival,
                              old, rel, slotRel, marked, cur, blocks, helping,
                              helpLast, acur, aheadBlocks, aheadLast, job,
                              took, slot, last >>

ReleaseMarks == /\ pc["message"] = "ReleaseMarks"
                /\ IF pending /= {}
                      THEN /\ \E x \in pending:
                                /\ marked' = arrival[x]
                                /\ pending' = pending \ {x}
                                /\ IF marked' % 2 = 1
                                      THEN /\ arrival[x] /= marked'
                                      ELSE /\ TRUE
                           /\ pc' = [pc EXCEPT !["message"] = "ReleaseMarks"]
                      ELSE /\ pc' = [pc EXCEPT !["message"] = "ReleaseInside"]
                           /\ UNCHANGED << pending, marked >>
                /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                nextEpoch, freed, jobPtr, queued, handed,
                                ready, remaining, inside, finishes, published,
                                arrival, old, rel, slotRel, cur, blocks,
                                helping, helpLast, acur, aheadBlocks,
                                aheadLast, job, took, slot, last >>

ReleaseInside == /\ pc["message"] = "ReleaseInside"
                 /\ inside[rel] = 0
                 /\ pc' = [pc EXCEPT !["message"] = "ReleaseJob"]
                 /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                 nextEpoch, freed, jobPtr, queued, handed,
                                 ready, remaining, inside, finishes, published,
                                 arrival, old, rel, slotRel, pending, marked,
                                 cur, blocks, helping, helpLast, acur,
                                 aheadBlocks, aheadLast, job, took, slot, last >>

Free == /\ pc["message"] = "Free"
        /\ freed' = (freed \union {old})
        /\ pc' = [pc EXCEPT !["message"] = "Publish"]
        /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                        nextEpoch, jobPtr, queued, handed, ready, remaining,
                        inside, finishes, published, arrival, old, rel,
                        slotRel, pending, marked, cur, blocks, helping,
                        helpLast, acur, aheadBlocks, aheadLast, job, took,
                        slot, last >>

Publisher == Publish \/ Swap \/ SetAhead \/ Retire \/ ReleaseJob
                \/ ReleaseCas \/ ReleaseMarks \/ ReleaseInside \/ Free

Piece == /\ pc["audio"] = "Piece"
         /\ IF blocks < MaxBlocks
               THEN /\ pc' = [pc EXCEPT !["audio"] = "Acquire"]
               ELSE /\ pc' = [pc EXCEPT !["audio"] = "Done"]
         /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                         nextEpoch, freed, jobPtr, queued, handed, ready,
                         remaining, inside, finishes, published, arrival, old,
                         rel, slotRel, pending, marked, cur, blocks, helping,
                         helpLast, acur, aheadBlocks, aheadLast, job, took,
                         slot, last >>

Acquire == /\ pc["audio"] = "Acquire"
           /\ cur' = JobOf(pointer, 1)
           /\ pointer' = NULL
           /\ blocks' = blocks + 1
           /\ pc' = [pc EXCEPT !["audio"] = "Help"]
           /\ UNCHANGED << storage, aheadEpoch, aheadBusy, live, nextEpoch,
                           freed, jobPtr, queued, handed, ready, remaining,
                           inside, finishes, published, arrival, old, rel,
                           slotRel, pending, marked, helping, helpLast, acur,
                           aheadBlocks, aheadLast, job, took, slot, last >>

Help == /\ pc["audio"] = "Help"
        /\ \/ /\ TRUE
              /\ pc' = [pc EXCEPT !["audio"] = "Render"]
              /\ UNCHANGED <<queued, ready, helping>>
           \/ /\ helping' = JobOf(EpochOf(cur), 2)
              /\ Assert(Touchable(helping'),
                        "Failure of assertion at line 107, column 13.")
              /\ IF ready[helping'] > 0
                    THEN /\ ready' = [ready EXCEPT ![helping'] = ready[helping'] - 1]
                         /\ queued' = queued - 1
                         /\ pc' = [pc EXCEPT !["audio"] = "HelpRun"]
                    ELSE /\ pc' = [pc EXCEPT !["audio"] = "Render"]
                         /\ UNCHANGED << queued, ready >>
        /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                        nextEpoch, freed, jobPtr, handed, remaining, inside,
                        finishes, published, arrival, old, rel, slotRel,
                        pending, marked, cur, blocks, helpLast, acur,
                        aheadBlocks, aheadLast, job, took, slot, last >>

HelpRun == /\ pc["audio"] = "HelpRun"
           /\ Assert(Touchable(helping),
                     "Failure of assertion at line 112, column 17.")
           /\ helpLast' = (remaining[helping] = 1)
           /\ remaining' = [remaining EXCEPT ![helping] = remaining[helping] - 1]
           /\ pc' = [pc EXCEPT !["audio"] = "HelpFinishBlock"]
           /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                           nextEpoch, freed, jobPtr, queued, handed, ready,
                           inside, finishes, published, arrival, old, rel,
                           slotRel, pending, marked, cur, blocks, helping,
                           acur, aheadBlocks, aheadLast, job, took, slot, last >>

HelpFinishBlock == /\ pc["audio"] = "HelpFinishBlock"
                   /\ IF helpLast
                         THEN /\ Assert(Touchable(helping) /\ finishes[helping] = 0,
                                        "Failure of assertion at line 117, column 21.")
                              /\ finishes' = [finishes EXCEPT ![helping] = 1]
                              /\ pc' = [pc EXCEPT !["audio"] = "HelpPublished"]
                         ELSE /\ pc' = [pc EXCEPT !["audio"] = "Render"]
                              /\ UNCHANGED finishes
                   /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy,
                                   live, nextEpoch, freed, jobPtr, queued,
                                   handed, ready, remaining, inside, published,
                                   arrival, old, rel, slotRel, pending, marked,
                                   cur, blocks, helping, helpLast, acur,
                                   aheadBlocks, aheadLast, job, took, slot,
                                   last >>

HelpPublished == /\ pc["audio"] = "HelpPublished"
                 /\ published' = [published EXCEPT ![helping] = TRUE]
                 /\ pc' = [pc EXCEPT !["audio"] = "Render"]
                 /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                 nextEpoch, freed, jobPtr, queued, handed,
                                 ready, remaining, inside, finishes, arrival,
                                 old, rel, slotRel, pending, marked, cur,
                                 blocks, helping, helpLast, acur, aheadBlocks,
                                 aheadLast, job, took, slot, last >>

Render == /\ pc["audio"] = "Render"
          /\ Assert(Touchable(cur),
                    "Failure of assertion at line 125, column 9.")
          /\ \/ /\ TRUE
                /\ pc' = [pc EXCEPT !["audio"] = "Release"]
                /\ UNCHANGED <<queued, handed, ready, remaining>>
             \/ /\ /\ ready' = [ready EXCEPT ![cur] = Tasks]
                   /\ remaining' = [remaining EXCEPT ![cur] = Tasks]
                /\ queued' = queued + Tasks
                /\ handed' = [handed EXCEPT ![cur] = TRUE]
                /\ pc' = [pc EXCEPT !["audio"] = "StoreJob"]
          /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                          nextEpoch, freed, jobPtr, inside, finishes,
                          published, arrival, old, rel, slotRel, pending,
                          marked, cur, blocks, helping, helpLast, acur,
                          aheadBlocks, aheadLast, job, took, slot, last >>

StoreJob == /\ pc["audio"] = "StoreJob"
            /\ jobPtr' = [jobPtr EXCEPT ![1] = cur]
            /\ pc' = [pc EXCEPT !["audio"] = "Finish"]
            /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                            nextEpoch, freed, queued, handed, ready, remaining,
                            inside, finishes, published, arrival, old, rel,
                            slotRel, pending, marked, cur, blocks, helping,
                            helpLast, acur, aheadBlocks, aheadLast, job, took,
                            slot, last >>

Finish == /\ pc["audio"] = "Finish"
          /\ IF remaining[cur] > 0
                THEN /\ Assert(Touchable(cur),
                               "Failure of assertion at line 136, column 17.")
                     /\ IF ready[cur] > 0
                           THEN /\ ready' = [ready EXCEPT ![cur] = ready[cur] - 1]
                                /\ queued' = queued - 1
                                /\ pc' = [pc EXCEPT !["audio"] = "CallerRun"]
                           ELSE /\ pc' = [pc EXCEPT !["audio"] = "Finish"]
                                /\ UNCHANGED << queued, ready >>
                ELSE /\ pc' = [pc EXCEPT !["audio"] = "Release"]
                     /\ UNCHANGED << queued, ready >>
          /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                          nextEpoch, freed, jobPtr, handed, remaining, inside,
                          finishes, published, arrival, old, rel, slotRel,
                          pending, marked, cur, blocks, helping, helpLast,
                          acur, aheadBlocks, aheadLast, job, took, slot, last >>

CallerRun == /\ pc["audio"] = "CallerRun"
             /\ remaining' = [remaining EXCEPT ![cur] = remaining[cur] - 1]
             /\ pc' = [pc EXCEPT !["audio"] = "Finish"]
             /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                             nextEpoch, freed, jobPtr, queued, handed, ready,
                             inside, finishes, published, arrival, old, rel,
                             slotRel, pending, marked, cur, blocks, helping,
                             helpLast, acur, aheadBlocks, aheadLast, job, took,
                             slot, last >>

Release == /\ pc["audio"] = "Release"
           /\ pointer' = EpochOf(cur)
           /\ pc' = [pc EXCEPT !["audio"] = "Piece"]
           /\ UNCHANGED << storage, aheadEpoch, aheadBusy, live, nextEpoch,
                           freed, jobPtr, queued, handed, ready, remaining,
                           inside, finishes, published, arrival, old, rel,
                           slotRel, pending, marked, cur, blocks, helping,
                           helpLast, acur, aheadBlocks, aheadLast, job, took,
                           slot, last >>

Audio == Piece \/ Acquire \/ Help \/ HelpRun \/ HelpFinishBlock
            \/ HelpPublished \/ Render \/ StoreJob \/ Finish \/ CallerRun
            \/ Release

AheadPiece == /\ pc["ahead"] = "AheadPiece"
              /\ IF aheadBlocks < MaxAheadBlocks
                    THEN /\ pc' = [pc EXCEPT !["ahead"] = "AheadAcquire"]
                    ELSE /\ pc' = [pc EXCEPT !["ahead"] = "Done"]
              /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                              nextEpoch, freed, jobPtr, queued, handed, ready,
                              remaining, inside, finishes, published, arrival,
                              old, rel, slotRel, pending, marked, cur, blocks,
                              helping, helpLast, acur, aheadBlocks, aheadLast,
                              job, took, slot, last >>

AheadAcquire == /\ pc["ahead"] = "AheadAcquire"
                /\ ~aheadBusy
                /\ aheadBusy' = TRUE
                /\ acur' = JobOf(aheadEpoch, 2)
                /\ aheadBlocks' = aheadBlocks + 1
                /\ pc' = [pc EXCEPT !["ahead"] = "AheadRender"]
                /\ UNCHANGED << pointer, storage, aheadEpoch, live, nextEpoch,
                                freed, jobPtr, queued, handed, ready,
                                remaining, inside, finishes, published,
                                arrival, old, rel, slotRel, pending, marked,
                                cur, blocks, helping, helpLast, aheadLast, job,
                                took, slot, last >>

AheadRender == /\ pc["ahead"] = "AheadRender"
               /\ Assert(Touchable(acur),
                         "Failure of assertion at line 162, column 9.")
               /\ \/ /\ TRUE
                     /\ pc' = [pc EXCEPT !["ahead"] = "AheadRelease"]
                     /\ UNCHANGED <<queued, handed, ready, remaining, finishes, published>>
                  \/ /\ Assert(~handed[acur] \/ published[acur],
                               "Failure of assertion at line 167, column 13.")
                     /\ /\ ready' = [ready EXCEPT ![acur] = Tasks]
                        /\ remaining' = [remaining EXCEPT ![acur] = Tasks]
                     /\ finishes' = [finishes EXCEPT ![acur] = 0]
                     /\ published' = [published EXCEPT ![acur] = FALSE]
                     /\ queued' = queued + Tasks
                     /\ handed' = [handed EXCEPT ![acur] = TRUE]
                     /\ pc' = [pc EXCEPT !["ahead"] = "AheadStoreJob"]
               /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                               nextEpoch, freed, jobPtr, inside, arrival, old,
                               rel, slotRel, pending, marked, cur, blocks,
                               helping, helpLast, acur, aheadBlocks, aheadLast,
                               job, took, slot, last >>

AheadStoreJob == /\ pc["ahead"] = "AheadStoreJob"
                 /\ jobPtr' = [jobPtr EXCEPT ![2] = acur]
                 /\ pc' = [pc EXCEPT !["ahead"] = "AheadFinish"]
                 /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                 nextEpoch, freed, queued, handed, ready,
                                 remaining, inside, finishes, published,
                                 arrival, old, rel, slotRel, pending, marked,
                                 cur, blocks, helping, helpLast, acur,
                                 aheadBlocks, aheadLast, job, took, slot, last >>

AheadFinish == /\ pc["ahead"] = "AheadFinish"
               /\ IF remaining[acur] > 0
                     THEN /\ Assert(Touchable(acur),
                                    "Failure of assertion at line 177, column 17.")
                          /\ IF ready[acur] > 0
                                THEN /\ ready' = [ready EXCEPT ![acur] = ready[acur] - 1]
                                     /\ queued' = queued - 1
                                     /\ pc' = [pc EXCEPT !["ahead"] = "AheadCallerRun"]
                                ELSE /\ pc' = [pc EXCEPT !["ahead"] = "AheadFinish"]
                                     /\ UNCHANGED << queued, ready >>
                     ELSE /\ pc' = [pc EXCEPT !["ahead"] = "AheadAwait"]
                          /\ UNCHANGED << queued, ready >>
               /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                               nextEpoch, freed, jobPtr, handed, remaining,
                               inside, finishes, published, arrival, old, rel,
                               slotRel, pending, marked, cur, blocks, helping,
                               helpLast, acur, aheadBlocks, aheadLast, job,
                               took, slot, last >>

AheadCallerRun == /\ pc["ahead"] = "AheadCallerRun"
                  /\ aheadLast' = (remaining[acur] = 1)
                  /\ remaining' = [remaining EXCEPT ![acur] = remaining[acur] - 1]
                  /\ pc' = [pc EXCEPT !["ahead"] = "AheadFinishBlock"]
                  /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy,
                                  live, nextEpoch, freed, jobPtr, queued,
                                  handed, ready, inside, finishes, published,
                                  arrival, old, rel, slotRel, pending, marked,
                                  cur, blocks, helping, helpLast, acur,
                                  aheadBlocks, job, took, slot, last >>

AheadFinishBlock == /\ pc["ahead"] = "AheadFinishBlock"
                    /\ IF aheadLast
                          THEN /\ Assert(Touchable(acur) /\ finishes[acur] = 0,
                                         "Failure of assertion at line 186, column 25.")
                               /\ finishes' = [finishes EXCEPT ![acur] = 1]
                               /\ pc' = [pc EXCEPT !["ahead"] = "AheadPublished"]
                          ELSE /\ pc' = [pc EXCEPT !["ahead"] = "AheadFinish"]
                               /\ UNCHANGED finishes
                    /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy,
                                    live, nextEpoch, freed, jobPtr, queued,
                                    handed, ready, remaining, inside,
                                    published, arrival, old, rel, slotRel,
                                    pending, marked, cur, blocks, helping,
                                    helpLast, acur, aheadBlocks, aheadLast,
                                    job, took, slot, last >>

AheadPublished == /\ pc["ahead"] = "AheadPublished"
                  /\ published' = [published EXCEPT ![acur] = TRUE]
                  /\ pc' = [pc EXCEPT !["ahead"] = "AheadFinish"]
                  /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy,
                                  live, nextEpoch, freed, jobPtr, queued,
                                  handed, ready, remaining, inside, finishes,
                                  arrival, old, rel, slotRel, pending, marked,
                                  cur, blocks, helping, helpLast, acur,
                                  aheadBlocks, aheadLast, job, took, slot,
                                  last >>

AheadAwait == /\ pc["ahead"] = "AheadAwait"
              /\ published[acur]
              /\ Assert(finishes[acur] = 1,
                        "Failure of assertion at line 195, column 13.")
              /\ pc' = [pc EXCEPT !["ahead"] = "AheadRelease"]
              /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                              nextEpoch, freed, jobPtr, queued, handed, ready,
                              remaining, inside, finishes, published, arrival,
                              old, rel, slotRel, pending, marked, cur, blocks,
                              helping, helpLast, acur, aheadBlocks, aheadLast,
                              job, took, slot, last >>

AheadRelease == /\ pc["ahead"] = "AheadRelease"
                /\ aheadBusy' = FALSE
                /\ pc' = [pc EXCEPT !["ahead"] = "AheadPiece"]
                /\ UNCHANGED << pointer, storage, aheadEpoch, live, nextEpoch,
                                freed, jobPtr, queued, handed, ready,
                                remaining, inside, finishes, published,
                                arrival, old, rel, slotRel, pending, marked,
                                cur, blocks, helping, helpLast, acur,
                                aheadBlocks, aheadLast, job, took, slot, last >>

Ahead == AheadPiece \/ AheadAcquire \/ AheadRender \/ AheadStoreJob
            \/ AheadFinish \/ AheadCallerRun \/ AheadFinishBlock
            \/ AheadPublished \/ AheadAwait \/ AheadRelease

Wait(self) == /\ pc[self] = "Wait"
              /\ queued > 0
              /\ slot' = [slot EXCEPT ![self] = 1]
              /\ pc' = [pc EXCEPT ![self] = "TrySlot"]
              /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                              nextEpoch, freed, jobPtr, queued, handed, ready,
                              remaining, inside, finishes, published, arrival,
                              old, rel, slotRel, pending, marked, cur, blocks,
                              helping, helpLast, acur, aheadBlocks, aheadLast,
                              job, took, last >>

TrySlot(self) == /\ pc[self] = "TrySlot"
                 /\ IF slot[self] <= 2
                       THEN /\ pc' = [pc EXCEPT ![self] = "Arrive"]
                       ELSE /\ pc' = [pc EXCEPT ![self] = "Wait"]
                 /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                 nextEpoch, freed, jobPtr, queued, handed,
                                 ready, remaining, inside, finishes, published,
                                 arrival, old, rel, slotRel, pending, marked,
                                 cur, blocks, helping, helpLast, acur,
                                 aheadBlocks, aheadLast, job, took, slot, last >>

Arrive(self) == /\ pc[self] = "Arrive"
                /\ arrival' = [arrival EXCEPT ![self] = (arrival[self] + 1) % 4]
                /\ pc' = [pc EXCEPT ![self] = "LoadJob"]
                /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                nextEpoch, freed, jobPtr, queued, handed,
                                ready, remaining, inside, finishes, published,
                                old, rel, slotRel, pending, marked, cur,
                                blocks, helping, helpLast, acur, aheadBlocks,
                                aheadLast, job, took, slot, last >>

LoadJob(self) == /\ pc[self] = "LoadJob"
                 /\ job' = [job EXCEPT ![self] = jobPtr[slot[self]]]
                 /\ pc' = [pc EXCEPT ![self] = "CountIn"]
                 /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                 nextEpoch, freed, jobPtr, queued, handed,
                                 ready, remaining, inside, finishes, published,
                                 arrival, old, rel, slotRel, pending, marked,
                                 cur, blocks, helping, helpLast, acur,
                                 aheadBlocks, aheadLast, took, slot, last >>

CountIn(self) == /\ pc[self] = "CountIn"
                 /\ IF job[self] /= NULL
                       THEN /\ Assert(Touchable(job[self]),
                                      "Failure of assertion at line 218, column 17.")
                            /\ inside' = [inside EXCEPT ![job[self]] = inside[job[self]] + 1]
                       ELSE /\ TRUE
                            /\ UNCHANGED inside
                 /\ pc' = [pc EXCEPT ![self] = "Arrived"]
                 /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                 nextEpoch, freed, jobPtr, queued, handed,
                                 ready, remaining, finishes, published,
                                 arrival, old, rel, slotRel, pending, marked,
                                 cur, blocks, helping, helpLast, acur,
                                 aheadBlocks, aheadLast, job, took, slot, last >>

Arrived(self) == /\ pc[self] = "Arrived"
                 /\ arrival' = [arrival EXCEPT ![self] = (arrival[self] + 1) % 4]
                 /\ IF job[self] = NULL
                       THEN /\ slot' = [slot EXCEPT ![self] = slot[self] + 1]
                            /\ pc' = [pc EXCEPT ![self] = "TrySlot"]
                       ELSE /\ pc' = [pc EXCEPT ![self] = "TakeOne"]
                            /\ slot' = slot
                 /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                 nextEpoch, freed, jobPtr, queued, handed,
                                 ready, remaining, inside, finishes, published,
                                 old, rel, slotRel, pending, marked, cur,
                                 blocks, helping, helpLast, acur, aheadBlocks,
                                 aheadLast, job, took, last >>

TakeOne(self) == /\ pc[self] = "TakeOne"
                 /\ Assert(Touchable(job[self]),
                           "Failure of assertion at line 228, column 13.")
                 /\ IF ready[job[self]] > 0
                       THEN /\ ready' = [ready EXCEPT ![job[self]] = ready[job[self]] - 1]
                            /\ queued' = queued - 1
                            /\ took' = [took EXCEPT ![self] = TRUE]
                       ELSE /\ took' = [took EXCEPT ![self] = FALSE]
                            /\ UNCHANGED << queued, ready >>
                 /\ pc' = [pc EXCEPT ![self] = "RunChain"]
                 /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy, live,
                                 nextEpoch, freed, jobPtr, handed, remaining,
                                 inside, finishes, published, arrival, old,
                                 rel, slotRel, pending, marked, cur, blocks,
                                 helping, helpLast, acur, aheadBlocks,
                                 aheadLast, job, slot, last >>

RunChain(self) == /\ pc[self] = "RunChain"
                  /\ IF took[self]
                        THEN /\ Assert(Touchable(job[self]),
                                       "Failure of assertion at line 238, column 17.")
                             /\ last' = [last EXCEPT ![self] = IsAhead(job[self]) /\ remaining[job[self]] = 1]
                             /\ remaining' = [remaining EXCEPT ![job[self]] = remaining[job[self]] - 1]
                        ELSE /\ TRUE
                             /\ UNCHANGED << remaining, last >>
                  /\ pc' = [pc EXCEPT ![self] = "WorkerFinishBlock"]
                  /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy,
                                  live, nextEpoch, freed, jobPtr, queued,
                                  handed, ready, inside, finishes, published,
                                  arrival, old, rel, slotRel, pending, marked,
                                  cur, blocks, helping, helpLast, acur,
                                  aheadBlocks, aheadLast, job, took, slot >>

WorkerFinishBlock(self) == /\ pc[self] = "WorkerFinishBlock"
                           /\ IF took[self] /\ last[self]
                                 THEN /\ Assert(Touchable(job[self]) /\ finishes[job[self]] = 0,
                                                "Failure of assertion at line 244, column 17.")
                                      /\ finishes' = [finishes EXCEPT ![job[self]] = 1]
                                      /\ pc' = [pc EXCEPT ![self] = "WorkerPublished"]
                                 ELSE /\ pc' = [pc EXCEPT ![self] = "CountOut"]
                                      /\ UNCHANGED finishes
                           /\ UNCHANGED << pointer, storage, aheadEpoch,
                                           aheadBusy, live, nextEpoch, freed,
                                           jobPtr, queued, handed, ready,
                                           remaining, inside, published,
                                           arrival, old, rel, slotRel, pending,
                                           marked, cur, blocks, helping,
                                           helpLast, acur, aheadBlocks,
                                           aheadLast, job, took, slot, last >>

WorkerPublished(self) == /\ pc[self] = "WorkerPublished"
                         /\ published' = [published EXCEPT ![job[self]] = TRUE]
                         /\ pc' = [pc EXCEPT ![self] = "CountOut"]
                         /\ UNCHANGED << pointer, storage, aheadEpoch,
                                         aheadBusy, live, nextEpoch, freed,
                                         jobPtr, queued, handed, ready,
                                         remaining, inside, finishes, arrival,
                                         old, rel, slotRel, pending, marked,
                                         cur, blocks, helping, helpLast, acur,
                                         aheadBlocks, aheadLast, job, took,
                                         slot, last >>

CountOut(self) == /\ pc[self] = "CountOut"
                  /\ Assert(Touchable(job[self]),
                            "Failure of assertion at line 250, column 13.")
                  /\ inside' = [inside EXCEPT ![job[self]] = inside[job[self]] - 1]
                  /\ IF took[self]
                        THEN /\ pc' = [pc EXCEPT ![self] = "Wait"]
                             /\ slot' = slot
                        ELSE /\ slot' = [slot EXCEPT ![self] = slot[self] + 1]
                             /\ pc' = [pc EXCEPT ![self] = "TrySlot"]
                  /\ UNCHANGED << pointer, storage, aheadEpoch, aheadBusy,
                                  live, nextEpoch, freed, jobPtr, queued,
                                  handed, ready, remaining, finishes,
                                  published, arrival, old, rel, slotRel,
                                  pending, marked, cur, blocks, helping,
                                  helpLast, acur, aheadBlocks, aheadLast, job,
                                  took, last >>

Worker(self) == Wait(self) \/ TrySlot(self) \/ Arrive(self)
                   \/ LoadJob(self) \/ CountIn(self) \/ Arrived(self)
                   \/ TakeOne(self) \/ RunChain(self)
                   \/ WorkerFinishBlock(self) \/ WorkerPublished(self)
                   \/ CountOut(self)

Next == Publisher \/ Audio \/ Ahead
           \/ (\E self \in Workers: Worker(self))

Spec == /\ Init /\ [][Next]_vars
        /\ SF_vars(Publisher)
        /\ WF_vars(Audio)
        /\ WF_vars(Ahead)
        /\ \A self \in Workers : WF_vars(Worker(self))

\* END TRANSLATION

\* Every epoch that stopped being live is eventually destroyed.
AllRetiredFreed == <>[](Retired \subseteq freed)

\* Both callers finish every block they start.
CallersFinish == <>(pc["audio"] = "Done" /\ pc["ahead"] = "Done")

WorkerSymmetry == Permutations(Workers)

=============================================================================
