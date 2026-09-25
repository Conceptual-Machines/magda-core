---------------------------- MODULE PlanSwap ----------------------------
(* The native engine's plan publish and the retirement of the epoch it replaces.
   Models EngineSession::publish/process (magda/engine/exec/EngineSession.cpp),
   farbot NonRealtimeMutatable (third_party/farbot/.../RealtimeObject.tcc), and
   RenderThreadPool render/takeWork/release with ParallelPlanExecutor::letGoOfPool.
   One label per seq_cst atomic operation; everything between labels is one step. *)
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Workers, MaxEpoch, MaxBlocks, Tasks

NULL == 0
Epochs == 1..MaxEpoch

(* --algorithm PlanSwap
variables
    pointer = 1,                        \* farbot `pointer`; NULL while the audio thread holds it
    storage = 1,                        \* farbot `storage`, publisher-owned
    live = 1,                           \* EngineSession::live_
    nextEpoch = 2,
    freed = {},                         \* epochs whose PreparedRender has been destroyed
    jobPtr = NULL,                      \* RenderThreadPool::job_, never cleared at block end
    queued = 0,                         \* RenderThreadPool::queued_
    handed = [e \in Epochs |-> FALSE],  \* ParallelPlanExecutor::handedToPool_
    ready = [e \in Epochs |-> 0],       \* ops in the executor's ready_ queue
    remaining = [e \in Epochs |-> 0],   \* ParallelPlanExecutor::remaining_
    inside = [e \in Epochs |-> 0],      \* Job::workersInside
    arrival = [w \in Workers |-> 0];    \* Worker::arrival, mod 4: odd and the arrival it names

define
    Touchable(e) == e \notin freed
    Retired == { e \in Epochs : e < live }
end define;

\* EngineSession::publish, message thread. The old epoch dies at `live_ = ...`.
fair+ process Publisher = "message"
variables old = NULL, pending = {}, marked = 0;
begin
Publish:
    while nextEpoch <= MaxEpoch do
    Swap:       \* nonRealtimeReplace: CAS spins until the audio thread has released
        await pointer = storage;
        pointer := nextEpoch;
        storage := nextEpoch;
    Retire:     \* live_ = std::move(prepared): ~ParallelPlanExecutor -> letGoOfPool
        old := live;
        live := nextEpoch;
        nextEpoch := nextEpoch + 1;
        if ~handed[old] then goto Free; end if;
    ReleaseCas: \* release(): job_.compare_exchange_strong(&job, nullptr)
        if jobPtr = old then jobPtr := NULL; end if;
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
        await inside[old] = 0;
    Free:
        freed := freed \union {old};
    end while;
end process;

\* EngineSession::process, one acquire per piece of a driver callback.
fair process Audio = "audio"
variables cur = NULL, blocks = 0;
begin
Piece:
    while blocks < MaxBlocks do
    Acquire:    \* realtimeAcquire: pointer.exchange(nullptr)
        cur := pointer;
        pointer := NULL;
        blocks := blocks + 1;
    Render:
        assert Touchable(cur);
        either
            skip;               \* renderInPlanOrder: the pool is not used
        or
            ready[cur] := Tasks || remaining[cur] := Tasks;     \* startSchedule
            queued := queued + Tasks;
            handed[cur] := TRUE;
    StoreJob:   \* render(): job_.store(&job)
            jobPtr := cur;
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
        pointer := cur;
    end while;
end process;

\* RenderThreadPool::Worker::run -> takeWork
fair process Worker \in Workers
variables job = NULL, took = FALSE;
begin
Wait:
    while TRUE do
        await queued > 0;
    Arrive:
        arrival[self] := (arrival[self] + 1) % 4;
    LoadJob:
        job := jobPtr;
    CountIn:
        if job /= NULL then
            assert Touchable(job);
            inside[job] := inside[job] + 1;
        end if;
    Arrived:
        arrival[self] := (arrival[self] + 1) % 4;
        if job = NULL then goto Wait; end if;
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
            remaining[job] := remaining[job] - 1;
        end if;
    CountOut:
        assert Touchable(job);
        inside[job] := inside[job] - 1;
    end while;
end process;
end algorithm; *)
\* BEGIN TRANSLATION (chksum(pcal) = "b121d8ef" /\ chksum(tla) = "d6364722")
VARIABLES pointer, storage, live, nextEpoch, freed, jobPtr, queued, handed,
          ready, remaining, inside, arrival, pc

(* define statement *)
Touchable(e) == e \notin freed
Retired == { e \in Epochs : e < live }

VARIABLES old, pending, marked, cur, blocks, job, took

vars == << pointer, storage, live, nextEpoch, freed, jobPtr, queued, handed,
           ready, remaining, inside, arrival, pc, old, pending, marked, cur,
           blocks, job, took >>

ProcSet == {"message"} \cup {"audio"} \cup (Workers)

Init == (* Global variables *)
        /\ pointer = 1
        /\ storage = 1
        /\ live = 1
        /\ nextEpoch = 2
        /\ freed = {}
        /\ jobPtr = NULL
        /\ queued = 0
        /\ handed = [e \in Epochs |-> FALSE]
        /\ ready = [e \in Epochs |-> 0]
        /\ remaining = [e \in Epochs |-> 0]
        /\ inside = [e \in Epochs |-> 0]
        /\ arrival = [w \in Workers |-> 0]
        (* Process Publisher *)
        /\ old = NULL
        /\ pending = {}
        /\ marked = 0
        (* Process Audio *)
        /\ cur = NULL
        /\ blocks = 0
        (* Process Worker *)
        /\ job = [self \in Workers |-> NULL]
        /\ took = [self \in Workers |-> FALSE]
        /\ pc = [self \in ProcSet |-> CASE self = "message" -> "Publish"
                                        [] self = "audio" -> "Piece"
                                        [] self \in Workers -> "Wait"]

Publish == /\ pc["message"] = "Publish"
           /\ IF nextEpoch <= MaxEpoch
                 THEN /\ pc' = [pc EXCEPT !["message"] = "Swap"]
                 ELSE /\ pc' = [pc EXCEPT !["message"] = "Done"]
           /\ UNCHANGED << pointer, storage, live, nextEpoch, freed, jobPtr,
                           queued, handed, ready, remaining, inside, arrival,
                           old, pending, marked, cur, blocks, job, took >>

Swap == /\ pc["message"] = "Swap"
        /\ pointer = storage
        /\ pointer' = nextEpoch
        /\ storage' = nextEpoch
        /\ pc' = [pc EXCEPT !["message"] = "Retire"]
        /\ UNCHANGED << live, nextEpoch, freed, jobPtr, queued, handed, ready,
                        remaining, inside, arrival, old, pending, marked, cur,
                        blocks, job, took >>

Retire == /\ pc["message"] = "Retire"
          /\ old' = live
          /\ live' = nextEpoch
          /\ nextEpoch' = nextEpoch + 1
          /\ IF ~handed[old']
                THEN /\ pc' = [pc EXCEPT !["message"] = "Free"]
                ELSE /\ pc' = [pc EXCEPT !["message"] = "ReleaseCas"]
          /\ UNCHANGED << pointer, storage, freed, jobPtr, queued, handed,
                          ready, remaining, inside, arrival, pending, marked,
                          cur, blocks, job, took >>

ReleaseCas == /\ pc["message"] = "ReleaseCas"
              /\ IF jobPtr = old
                    THEN /\ jobPtr' = NULL
                    ELSE /\ TRUE
                         /\ UNCHANGED jobPtr
              /\ pending' = Workers
              /\ pc' = [pc EXCEPT !["message"] = "ReleaseMarks"]
              /\ UNCHANGED << pointer, storage, live, nextEpoch, freed, queued,
                              handed, ready, remaining, inside, arrival, old,
                              marked, cur, blocks, job, took >>

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
                /\ UNCHANGED << pointer, storage, live, nextEpoch, freed,
                                jobPtr, queued, handed, ready, remaining,
                                inside, arrival, old, cur, blocks, job, took >>

ReleaseInside == /\ pc["message"] = "ReleaseInside"
                 /\ inside[old] = 0
                 /\ pc' = [pc EXCEPT !["message"] = "Free"]
                 /\ UNCHANGED << pointer, storage, live, nextEpoch, freed,
                                 jobPtr, queued, handed, ready, remaining,
                                 inside, arrival, old, pending, marked, cur,
                                 blocks, job, took >>

Free == /\ pc["message"] = "Free"
        /\ freed' = (freed \union {old})
        /\ pc' = [pc EXCEPT !["message"] = "Publish"]
        /\ UNCHANGED << pointer, storage, live, nextEpoch, jobPtr, queued,
                        handed, ready, remaining, inside, arrival, old,
                        pending, marked, cur, blocks, job, took >>

Publisher == Publish \/ Swap \/ Retire \/ ReleaseCas \/ ReleaseMarks
                \/ ReleaseInside \/ Free

Piece == /\ pc["audio"] = "Piece"
         /\ IF blocks < MaxBlocks
               THEN /\ pc' = [pc EXCEPT !["audio"] = "Acquire"]
               ELSE /\ pc' = [pc EXCEPT !["audio"] = "Done"]
         /\ UNCHANGED << pointer, storage, live, nextEpoch, freed, jobPtr,
                         queued, handed, ready, remaining, inside, arrival,
                         old, pending, marked, cur, blocks, job, took >>

Acquire == /\ pc["audio"] = "Acquire"
           /\ cur' = pointer
           /\ pointer' = NULL
           /\ blocks' = blocks + 1
           /\ pc' = [pc EXCEPT !["audio"] = "Render"]
           /\ UNCHANGED << storage, live, nextEpoch, freed, jobPtr, queued,
                           handed, ready, remaining, inside, arrival, old,
                           pending, marked, job, took >>

Render == /\ pc["audio"] = "Render"
          /\ Assert(Touchable(cur),
                    "Failure of assertion at line 80, column 9.")
          /\ \/ /\ TRUE
                /\ pc' = [pc EXCEPT !["audio"] = "Release"]
                /\ UNCHANGED <<queued, handed, ready, remaining>>
             \/ /\ /\ ready' = [ready EXCEPT ![cur] = Tasks]
                   /\ remaining' = [remaining EXCEPT ![cur] = Tasks]
                /\ queued' = queued + Tasks
                /\ handed' = [handed EXCEPT ![cur] = TRUE]
                /\ pc' = [pc EXCEPT !["audio"] = "StoreJob"]
          /\ UNCHANGED << pointer, storage, live, nextEpoch, freed, jobPtr,
                          inside, arrival, old, pending, marked, cur, blocks,
                          job, took >>

StoreJob == /\ pc["audio"] = "StoreJob"
            /\ jobPtr' = cur
            /\ pc' = [pc EXCEPT !["audio"] = "Finish"]
            /\ UNCHANGED << pointer, storage, live, nextEpoch, freed, queued,
                            handed, ready, remaining, inside, arrival, old,
                            pending, marked, cur, blocks, job, took >>

Finish == /\ pc["audio"] = "Finish"
          /\ IF remaining[cur] > 0
                THEN /\ Assert(Touchable(cur),
                               "Failure of assertion at line 91, column 17.")
                     /\ IF ready[cur] > 0
                           THEN /\ ready' = [ready EXCEPT ![cur] = ready[cur] - 1]
                                /\ queued' = queued - 1
                                /\ pc' = [pc EXCEPT !["audio"] = "CallerRun"]
                           ELSE /\ pc' = [pc EXCEPT !["audio"] = "Finish"]
                                /\ UNCHANGED << queued, ready >>
                ELSE /\ pc' = [pc EXCEPT !["audio"] = "Release"]
                     /\ UNCHANGED << queued, ready >>
          /\ UNCHANGED << pointer, storage, live, nextEpoch, freed, jobPtr,
                          handed, remaining, inside, arrival, old, pending,
                          marked, cur, blocks, job, took >>

CallerRun == /\ pc["audio"] = "CallerRun"
             /\ remaining' = [remaining EXCEPT ![cur] = remaining[cur] - 1]
             /\ pc' = [pc EXCEPT !["audio"] = "Finish"]
             /\ UNCHANGED << pointer, storage, live, nextEpoch, freed, jobPtr,
                             queued, handed, ready, inside, arrival, old,
                             pending, marked, cur, blocks, job, took >>

Release == /\ pc["audio"] = "Release"
           /\ pointer' = cur
           /\ pc' = [pc EXCEPT !["audio"] = "Piece"]
           /\ UNCHANGED << storage, live, nextEpoch, freed, jobPtr, queued,
                           handed, ready, remaining, inside, arrival, old,
                           pending, marked, cur, blocks, job, took >>

Audio == Piece \/ Acquire \/ Render \/ StoreJob \/ Finish \/ CallerRun
            \/ Release

Wait(self) == /\ pc[self] = "Wait"
              /\ queued > 0
              /\ pc' = [pc EXCEPT ![self] = "Arrive"]
              /\ UNCHANGED << pointer, storage, live, nextEpoch, freed, jobPtr,
                              queued, handed, ready, remaining, inside,
                              arrival, old, pending, marked, cur, blocks, job,
                              took >>

Arrive(self) == /\ pc[self] = "Arrive"
                /\ arrival' = [arrival EXCEPT ![self] = (arrival[self] + 1) % 4]
                /\ pc' = [pc EXCEPT ![self] = "LoadJob"]
                /\ UNCHANGED << pointer, storage, live, nextEpoch, freed,
                                jobPtr, queued, handed, ready, remaining,
                                inside, old, pending, marked, cur, blocks, job,
                                took >>

LoadJob(self) == /\ pc[self] = "LoadJob"
                 /\ job' = [job EXCEPT ![self] = jobPtr]
                 /\ pc' = [pc EXCEPT ![self] = "CountIn"]
                 /\ UNCHANGED << pointer, storage, live, nextEpoch, freed,
                                 jobPtr, queued, handed, ready, remaining,
                                 inside, arrival, old, pending, marked, cur,
                                 blocks, took >>

CountIn(self) == /\ pc[self] = "CountIn"
                 /\ IF job[self] /= NULL
                       THEN /\ Assert(Touchable(job[self]),
                                      "Failure of assertion at line 118, column 13.")
                            /\ inside' = [inside EXCEPT ![job[self]] = inside[job[self]] + 1]
                       ELSE /\ TRUE
                            /\ UNCHANGED inside
                 /\ pc' = [pc EXCEPT ![self] = "Arrived"]
                 /\ UNCHANGED << pointer, storage, live, nextEpoch, freed,
                                 jobPtr, queued, handed, ready, remaining,
                                 arrival, old, pending, marked, cur, blocks,
                                 job, took >>

Arrived(self) == /\ pc[self] = "Arrived"
                 /\ arrival' = [arrival EXCEPT ![self] = (arrival[self] + 1) % 4]
                 /\ IF job[self] = NULL
                       THEN /\ pc' = [pc EXCEPT ![self] = "Wait"]
                       ELSE /\ pc' = [pc EXCEPT ![self] = "TakeOne"]
                 /\ UNCHANGED << pointer, storage, live, nextEpoch, freed,
                                 jobPtr, queued, handed, ready, remaining,
                                 inside, old, pending, marked, cur, blocks,
                                 job, took >>

TakeOne(self) == /\ pc[self] = "TakeOne"
                 /\ Assert(Touchable(job[self]),
                           "Failure of assertion at line 125, column 9.")
                 /\ IF ready[job[self]] > 0
                       THEN /\ ready' = [ready EXCEPT ![job[self]] = ready[job[self]] - 1]
                            /\ queued' = queued - 1
                            /\ took' = [took EXCEPT ![self] = TRUE]
                       ELSE /\ took' = [took EXCEPT ![self] = FALSE]
                            /\ UNCHANGED << queued, ready >>
                 /\ pc' = [pc EXCEPT ![self] = "RunChain"]
                 /\ UNCHANGED << pointer, storage, live, nextEpoch, freed,
                                 jobPtr, handed, remaining, inside, arrival,
                                 old, pending, marked, cur, blocks, job >>

RunChain(self) == /\ pc[self] = "RunChain"
                  /\ IF took[self]
                        THEN /\ Assert(Touchable(job[self]),
                                       "Failure of assertion at line 135, column 13.")
                             /\ remaining' = [remaining EXCEPT ![job[self]] = remaining[job[self]] - 1]
                        ELSE /\ TRUE
                             /\ UNCHANGED remaining
                  /\ pc' = [pc EXCEPT ![self] = "CountOut"]
                  /\ UNCHANGED << pointer, storage, live, nextEpoch, freed,
                                  jobPtr, queued, handed, ready, inside,
                                  arrival, old, pending, marked, cur, blocks,
                                  job, took >>

CountOut(self) == /\ pc[self] = "CountOut"
                  /\ Assert(Touchable(job[self]),
                            "Failure of assertion at line 139, column 9.")
                  /\ inside' = [inside EXCEPT ![job[self]] = inside[job[self]] - 1]
                  /\ pc' = [pc EXCEPT ![self] = "Wait"]
                  /\ UNCHANGED << pointer, storage, live, nextEpoch, freed,
                                  jobPtr, queued, handed, ready, remaining,
                                  arrival, old, pending, marked, cur, blocks,
                                  job, took >>

Worker(self) == Wait(self) \/ Arrive(self) \/ LoadJob(self)
                   \/ CountIn(self) \/ Arrived(self) \/ TakeOne(self)
                   \/ RunChain(self) \/ CountOut(self)

Next == Publisher \/ Audio
           \/ (\E self \in Workers: Worker(self))

Spec == /\ Init /\ [][Next]_vars
        /\ SF_vars(Publisher)
        /\ WF_vars(Audio)
        /\ \A self \in Workers : WF_vars(Worker(self))

\* END TRANSLATION

\* Every epoch that stopped being live is eventually destroyed.
AllRetiredFreed == <>[](Retired \subseteq freed)

WorkerSymmetry == Permutations(Workers)

=============================================================================
