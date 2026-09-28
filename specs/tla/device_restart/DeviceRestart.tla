--------------------------- MODULE DeviceRestart ---------------------------
EXTENDS Naturals, TLC
CONSTANTS MaxRestarts, MaxCallbacks

(* EngineHost::Impl::{audioDeviceAboutToStart,audioDeviceStopped,
   handleAsyncUpdate,rebuild,audioDeviceIOCallbackWithContext}, in
   magda/daw/engine/host/EngineHost.cpp, under AudioIOService's manager.
   JUCE AudioDeviceManager serializes audio/start/stop with audioCallbackLock;
   removeAudioCallback takes the same lock before unregistering.
   Rate and size are separate atomic observations. A physical restart can
   interrupt a handler; the generation check and ready store are separate too.
   Restart notifications while detached are excluded: reattachment announces
   the current device. See README.md for the environment contract. *)
(* --algorithm DeviceRestart
variables
    registered = TRUE, running = TRUE, mutex = "none",
    rate = 1, size = 1, generation = 1, ready = 1, pending = FALSE,
    preparedRate = 1, preparedSize = 1,
    session = 1, nextSession = 2, freed = {}, restarts = 0;

fair process Device = "device"
begin
Restart:
    while restarts < MaxRestarts do
        await registered /\ mutex = "none";
        mutex := "device";
        running := FALSE;
        generation := generation + 1;
        pending := TRUE;
StartRate:
        with r \in {1, 2} do rate := r; end with;
StartSize:
        with s \in {1, 2} do size := s; end with;
StartGeneration:
        generation := generation + 1;
        pending := TRUE;
        running := TRUE;
Unlock:
        mutex := "none";
        restarts := restarts + 1;
    end while;
end process;

fair process Message = "message"
variables observed = 0, wantedRate = 0, wantedSize = 0;
begin
Dispatch:
    while TRUE do
        await pending;
        pending := FALSE; \* AsyncUpdater clears delivery before calling the handler
CaptureGeneration:
        observed := generation;
ReadRate:
        wantedRate := rate;
ReadSize:
        wantedSize := size;
Decide:
        if session = 0 \/ preparedRate /= wantedRate \/ preparedSize /= wantedSize then
Remove:
            await mutex = "none";
            registered := FALSE;
Stopped:
            running := FALSE;
            generation := generation + 1;
            pending := TRUE;
Destroy:
            freed := freed \union {session};
            session := 0;
Prepare:
            preparedRate := wantedRate;
            preparedSize := wantedSize;
            session := nextSession;
            nextSession := nextSession + 1;
AboutToStart:
            \* addAudioCallback calls AboutToStart before inserting the callback.
            generation := generation + 1;
            pending := TRUE;
            running := TRUE;
Register:
            registered := TRUE;
            goto Dispatch; \* rebuild returns without opening the output gate
        end if;
CheckGeneration:
        if generation = observed then
ReadyStore:
            ready := observed;
        else
            pending := TRUE;
        end if;
    end while;
end process;

fair process Audio = "audio"
variables callbacks = 0, seen = 0, usingSession = 0;
begin
Callback:
    while callbacks < MaxCallbacks do
        await registered /\ running /\ mutex = "none";
        mutex := "audio";
ReadGeneration:
        seen := generation;
Gate:
        if seen /= 0 /\ ready = seen then
            usingSession := session;
Render:
            assert usingSession /= 0 /\ usingSession \notin freed;
            assert preparedRate = rate /\ preparedSize = size;
        end if;
Leave:
        mutex := "none";
        callbacks := callbacks + 1;
    end while;
end process;
end algorithm; *)
\* BEGIN TRANSLATION (chksum(pcal) = "ad1914ae" /\ chksum(tla) = "229befc9")
VARIABLES registered, running, mutex, rate, size, generation, ready, pending,
          preparedRate, preparedSize, session, nextSession, freed, restarts,
          pc, observed, wantedRate, wantedSize, callbacks, seen, usingSession

vars == << registered, running, mutex, rate, size, generation, ready, pending,
           preparedRate, preparedSize, session, nextSession, freed, restarts,
           pc, observed, wantedRate, wantedSize, callbacks, seen,
           usingSession >>

ProcSet == {"device"} \cup {"message"} \cup {"audio"}

Init == (* Global variables *)
        /\ registered = TRUE
        /\ running = TRUE
        /\ mutex = "none"
        /\ rate = 1
        /\ size = 1
        /\ generation = 1
        /\ ready = 1
        /\ pending = FALSE
        /\ preparedRate = 1
        /\ preparedSize = 1
        /\ session = 1
        /\ nextSession = 2
        /\ freed = {}
        /\ restarts = 0
        (* Process Message *)
        /\ observed = 0
        /\ wantedRate = 0
        /\ wantedSize = 0
        (* Process Audio *)
        /\ callbacks = 0
        /\ seen = 0
        /\ usingSession = 0
        /\ pc = [self \in ProcSet |-> CASE self = "device" -> "Restart"
                                        [] self = "message" -> "Dispatch"
                                        [] self = "audio" -> "Callback"]

Restart == /\ pc["device"] = "Restart"
           /\ IF restarts < MaxRestarts
                 THEN /\ registered /\ mutex = "none"
                      /\ mutex' = "device"
                      /\ running' = FALSE
                      /\ generation' = generation + 1
                      /\ pending' = TRUE
                      /\ pc' = [pc EXCEPT !["device"] = "StartRate"]
                 ELSE /\ pc' = [pc EXCEPT !["device"] = "Done"]
                      /\ UNCHANGED << running, mutex, generation, pending >>
           /\ UNCHANGED << registered, rate, size, ready, preparedRate,
                           preparedSize, session, nextSession, freed, restarts,
                           observed, wantedRate, wantedSize, callbacks, seen,
                           usingSession >>

StartRate == /\ pc["device"] = "StartRate"
             /\ \E r \in {1, 2}:
                  rate' = r
             /\ pc' = [pc EXCEPT !["device"] = "StartSize"]
             /\ UNCHANGED << registered, running, mutex, size, generation,
                             ready, pending, preparedRate, preparedSize,
                             session, nextSession, freed, restarts, observed,
                             wantedRate, wantedSize, callbacks, seen,
                             usingSession >>

StartSize == /\ pc["device"] = "StartSize"
             /\ \E s \in {1, 2}:
                  size' = s
             /\ pc' = [pc EXCEPT !["device"] = "StartGeneration"]
             /\ UNCHANGED << registered, running, mutex, rate, generation,
                             ready, pending, preparedRate, preparedSize,
                             session, nextSession, freed, restarts, observed,
                             wantedRate, wantedSize, callbacks, seen,
                             usingSession >>

StartGeneration == /\ pc["device"] = "StartGeneration"
                   /\ generation' = generation + 1
                   /\ pending' = TRUE
                   /\ running' = TRUE
                   /\ pc' = [pc EXCEPT !["device"] = "Unlock"]
                   /\ UNCHANGED << registered, mutex, rate, size, ready,
                                   preparedRate, preparedSize, session,
                                   nextSession, freed, restarts, observed,
                                   wantedRate, wantedSize, callbacks, seen,
                                   usingSession >>

Unlock == /\ pc["device"] = "Unlock"
          /\ mutex' = "none"
          /\ restarts' = restarts + 1
          /\ pc' = [pc EXCEPT !["device"] = "Restart"]
          /\ UNCHANGED << registered, running, rate, size, generation, ready,
                          pending, preparedRate, preparedSize, session,
                          nextSession, freed, observed, wantedRate, wantedSize,
                          callbacks, seen, usingSession >>

Device == Restart \/ StartRate \/ StartSize \/ StartGeneration \/ Unlock

Dispatch == /\ pc["message"] = "Dispatch"
            /\ pending
            /\ pending' = FALSE
            /\ pc' = [pc EXCEPT !["message"] = "CaptureGeneration"]
            /\ UNCHANGED << registered, running, mutex, rate, size, generation,
                            ready, preparedRate, preparedSize, session,
                            nextSession, freed, restarts, observed, wantedRate,
                            wantedSize, callbacks, seen, usingSession >>

CaptureGeneration == /\ pc["message"] = "CaptureGeneration"
                     /\ observed' = generation
                     /\ pc' = [pc EXCEPT !["message"] = "ReadRate"]
                     /\ UNCHANGED << registered, running, mutex, rate, size,
                                     generation, ready, pending, preparedRate,
                                     preparedSize, session, nextSession, freed,
                                     restarts, wantedRate, wantedSize,
                                     callbacks, seen, usingSession >>

ReadRate == /\ pc["message"] = "ReadRate"
            /\ wantedRate' = rate
            /\ pc' = [pc EXCEPT !["message"] = "ReadSize"]
            /\ UNCHANGED << registered, running, mutex, rate, size, generation,
                            ready, pending, preparedRate, preparedSize,
                            session, nextSession, freed, restarts, observed,
                            wantedSize, callbacks, seen, usingSession >>

ReadSize == /\ pc["message"] = "ReadSize"
            /\ wantedSize' = size
            /\ pc' = [pc EXCEPT !["message"] = "Decide"]
            /\ UNCHANGED << registered, running, mutex, rate, size, generation,
                            ready, pending, preparedRate, preparedSize,
                            session, nextSession, freed, restarts, observed,
                            wantedRate, callbacks, seen, usingSession >>

Decide == /\ pc["message"] = "Decide"
          /\ IF session = 0 \/ preparedRate /= wantedRate \/ preparedSize /= wantedSize
                THEN /\ pc' = [pc EXCEPT !["message"] = "Remove"]
                ELSE /\ pc' = [pc EXCEPT !["message"] = "CheckGeneration"]
          /\ UNCHANGED << registered, running, mutex, rate, size, generation,
                          ready, pending, preparedRate, preparedSize, session,
                          nextSession, freed, restarts, observed, wantedRate,
                          wantedSize, callbacks, seen, usingSession >>

Remove == /\ pc["message"] = "Remove"
          /\ mutex = "none"
          /\ registered' = FALSE
          /\ pc' = [pc EXCEPT !["message"] = "Stopped"]
          /\ UNCHANGED << running, mutex, rate, size, generation, ready,
                          pending, preparedRate, preparedSize, session,
                          nextSession, freed, restarts, observed, wantedRate,
                          wantedSize, callbacks, seen, usingSession >>

Stopped == /\ pc["message"] = "Stopped"
           /\ running' = FALSE
           /\ generation' = generation + 1
           /\ pending' = TRUE
           /\ pc' = [pc EXCEPT !["message"] = "Destroy"]
           /\ UNCHANGED << registered, mutex, rate, size, ready, preparedRate,
                           preparedSize, session, nextSession, freed, restarts,
                           observed, wantedRate, wantedSize, callbacks, seen,
                           usingSession >>

Destroy == /\ pc["message"] = "Destroy"
           /\ freed' = (freed \union {session})
           /\ session' = 0
           /\ pc' = [pc EXCEPT !["message"] = "Prepare"]
           /\ UNCHANGED << registered, running, mutex, rate, size, generation,
                           ready, pending, preparedRate, preparedSize,
                           nextSession, restarts, observed, wantedRate,
                           wantedSize, callbacks, seen, usingSession >>

Prepare == /\ pc["message"] = "Prepare"
           /\ preparedRate' = wantedRate
           /\ preparedSize' = wantedSize
           /\ session' = nextSession
           /\ nextSession' = nextSession + 1
           /\ pc' = [pc EXCEPT !["message"] = "AboutToStart"]
           /\ UNCHANGED << registered, running, mutex, rate, size, generation,
                           ready, pending, freed, restarts, observed,
                           wantedRate, wantedSize, callbacks, seen,
                           usingSession >>

AboutToStart == /\ pc["message"] = "AboutToStart"
                /\ generation' = generation + 1
                /\ pending' = TRUE
                /\ running' = TRUE
                /\ pc' = [pc EXCEPT !["message"] = "Register"]
                /\ UNCHANGED << registered, mutex, rate, size, ready,
                                preparedRate, preparedSize, session,
                                nextSession, freed, restarts, observed,
                                wantedRate, wantedSize, callbacks, seen,
                                usingSession >>

Register == /\ pc["message"] = "Register"
            /\ registered' = TRUE
            /\ pc' = [pc EXCEPT !["message"] = "Dispatch"]
            /\ UNCHANGED << running, mutex, rate, size, generation, ready,
                            pending, preparedRate, preparedSize, session,
                            nextSession, freed, restarts, observed, wantedRate,
                            wantedSize, callbacks, seen, usingSession >>

CheckGeneration == /\ pc["message"] = "CheckGeneration"
                   /\ IF generation = observed
                         THEN /\ pc' = [pc EXCEPT !["message"] = "ReadyStore"]
                              /\ UNCHANGED pending
                         ELSE /\ pending' = TRUE
                              /\ pc' = [pc EXCEPT !["message"] = "Dispatch"]
                   /\ UNCHANGED << registered, running, mutex, rate, size,
                                   generation, ready, preparedRate,
                                   preparedSize, session, nextSession, freed,
                                   restarts, observed, wantedRate, wantedSize,
                                   callbacks, seen, usingSession >>

ReadyStore == /\ pc["message"] = "ReadyStore"
              /\ ready' = observed
              /\ pc' = [pc EXCEPT !["message"] = "Dispatch"]
              /\ UNCHANGED << registered, running, mutex, rate, size,
                              generation, pending, preparedRate, preparedSize,
                              session, nextSession, freed, restarts, observed,
                              wantedRate, wantedSize, callbacks, seen,
                              usingSession >>

Message == Dispatch \/ CaptureGeneration \/ ReadRate \/ ReadSize \/ Decide
              \/ Remove \/ Stopped \/ Destroy \/ Prepare \/ AboutToStart
              \/ Register \/ CheckGeneration \/ ReadyStore

Callback == /\ pc["audio"] = "Callback"
            /\ IF callbacks < MaxCallbacks
                  THEN /\ registered /\ running /\ mutex = "none"
                       /\ mutex' = "audio"
                       /\ pc' = [pc EXCEPT !["audio"] = "ReadGeneration"]
                  ELSE /\ pc' = [pc EXCEPT !["audio"] = "Done"]
                       /\ mutex' = mutex
            /\ UNCHANGED << registered, running, rate, size, generation, ready,
                            pending, preparedRate, preparedSize, session,
                            nextSession, freed, restarts, observed, wantedRate,
                            wantedSize, callbacks, seen, usingSession >>

ReadGeneration == /\ pc["audio"] = "ReadGeneration"
                  /\ seen' = generation
                  /\ pc' = [pc EXCEPT !["audio"] = "Gate"]
                  /\ UNCHANGED << registered, running, mutex, rate, size,
                                  generation, ready, pending, preparedRate,
                                  preparedSize, session, nextSession, freed,
                                  restarts, observed, wantedRate, wantedSize,
                                  callbacks, usingSession >>

Gate == /\ pc["audio"] = "Gate"
        /\ IF seen /= 0 /\ ready = seen
              THEN /\ usingSession' = session
                   /\ pc' = [pc EXCEPT !["audio"] = "Render"]
              ELSE /\ pc' = [pc EXCEPT !["audio"] = "Leave"]
                   /\ UNCHANGED usingSession
        /\ UNCHANGED << registered, running, mutex, rate, size, generation,
                        ready, pending, preparedRate, preparedSize, session,
                        nextSession, freed, restarts, observed, wantedRate,
                        wantedSize, callbacks, seen >>

Render == /\ pc["audio"] = "Render"
          /\ Assert(usingSession /= 0 /\ usingSession \notin freed,
                    "Failure of assertion at line 106, column 13.")
          /\ Assert(preparedRate = rate /\ preparedSize = size,
                    "Failure of assertion at line 107, column 13.")
          /\ pc' = [pc EXCEPT !["audio"] = "Leave"]
          /\ UNCHANGED << registered, running, mutex, rate, size, generation,
                          ready, pending, preparedRate, preparedSize, session,
                          nextSession, freed, restarts, observed, wantedRate,
                          wantedSize, callbacks, seen, usingSession >>

Leave == /\ pc["audio"] = "Leave"
         /\ mutex' = "none"
         /\ callbacks' = callbacks + 1
         /\ pc' = [pc EXCEPT !["audio"] = "Callback"]
         /\ UNCHANGED << registered, running, rate, size, generation, ready,
                         pending, preparedRate, preparedSize, session,
                         nextSession, freed, restarts, observed, wantedRate,
                         wantedSize, seen, usingSession >>

Audio == Callback \/ ReadGeneration \/ Gate \/ Render \/ Leave

Next == Device \/ Message \/ Audio

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Device)
        /\ WF_vars(Message)
        /\ WF_vars(Audio)

\* END TRANSLATION

SessionOwned == session = 0 \/ session \notin freed
RebuildQuiescent == pc["message"] \in {"Stopped", "Destroy", "Prepare",
                                      "AboutToStart", "Register"} => mutex /= "audio"
EventuallyReady == <>[](restarts = MaxRestarts /\ registered /\ running /\
                        ready = generation /\ preparedRate = rate /\ preparedSize = size)
=============================================================================
