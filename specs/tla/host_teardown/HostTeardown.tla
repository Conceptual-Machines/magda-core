--------------------------- MODULE HostTeardown ---------------------------
EXTENDS Naturals, TLC
CONSTANTS MaxCallbacks, MaxMessages

(* EngineHost::Impl::~Impl / detach, and JUCE AsyncUpdater / AudioDeviceManager.
   All destruction, listeners, timer dispatch and handleAsyncUpdate run on the
   message thread. Audio can be mid-callback when destruction starts.
   Remove waits for that callback, then audioDeviceStopped posts another update.
   Cancelling clears shouldDeliver; it does NOT erase the queued message object.
   The cancelled message can dispatch after the owner is gone without calling it.
   This is the native host boundary, NOT all JUCE test-process teardown. *)
(* --algorithm HostTeardown
variables
    ownerAlive = TRUE, resourcesAlive = TRUE,
    registered = TRUE, inside = FALSE, listeners = TRUE, timer = TRUE,
    pending = FALSE, queued = FALSE, timerQueued = TRUE, destroyed = FALSE;

fair process Audio = "audio"
variables callbacks = 0;
begin
Callback:
    while callbacks < MaxCallbacks do
        await registered;
        inside := TRUE; \* holds AudioDeviceManager::audioCallbackLock
Render:
        assert ownerAlive /\ resourcesAlive;
Post:
        pending := TRUE;
        queued := TRUE;
Leave:
        inside := FALSE;
        callbacks := callbacks + 1;
    end while;
end process;

fair process Message = "message"
variables messages = 0;
begin
Loop:
    while messages < MaxMessages do
        either
            goto StopTimer;
        or
            if queued then
                queued := FALSE;
                if pending then
                    pending := FALSE;
Handle:
                    assert ownerAlive /\ resourcesAlive;
                end if;
            elsif timerQueued then
                timerQueued := FALSE;
                if timer then assert ownerAlive /\ resourcesAlive; end if;
            else
                \* A model-listener notification on this same message thread.
                if listeners then
                    pending := TRUE;
                    queued := TRUE;
                end if;
            end if;
NextMessage:
            messages := messages + 1;
        end either;
    end while;
StopTimer:
    timer := FALSE;
RemoveCallback:
    await ~inside;
    registered := FALSE;
Stopped:
    \* removeAudioCallback synchronously invokes audioDeviceStopped.
    pending := TRUE;
    queued := TRUE;
RemoveListeners:
    listeners := FALSE;
Cancel:
    pending := FALSE;
ReleaseResources:
    resourcesAlive := FALSE;
DestroyOwner:
    ownerAlive := FALSE;
    destroyed := TRUE;
LateDispatch:
    \* The JUCE message owns its delivery flag independently of the raw owner.
    if queued then
        queued := FALSE;
        if pending then
            pending := FALSE;
            assert ownerAlive /\ resourcesAlive;
        end if;
    end if;
LateTimer:
    if timerQueued then
        timerQueued := FALSE;
        if timer then assert ownerAlive /\ resourcesAlive; end if;
    end if;
end process;
end algorithm; *)
\* BEGIN TRANSLATION (chksum(pcal) = "d2fbee27" /\ chksum(tla) = "4d30bcbe")
VARIABLES ownerAlive, resourcesAlive, registered, inside, listeners, timer,
          pending, queued, timerQueued, destroyed, pc, callbacks, messages

vars == << ownerAlive, resourcesAlive, registered, inside, listeners, timer,
           pending, queued, timerQueued, destroyed, pc, callbacks, messages
        >>

ProcSet == {"audio"} \cup {"message"}

Init == (* Global variables *)
        /\ ownerAlive = TRUE
        /\ resourcesAlive = TRUE
        /\ registered = TRUE
        /\ inside = FALSE
        /\ listeners = TRUE
        /\ timer = TRUE
        /\ pending = FALSE
        /\ queued = FALSE
        /\ timerQueued = TRUE
        /\ destroyed = FALSE
        (* Process Audio *)
        /\ callbacks = 0
        (* Process Message *)
        /\ messages = 0
        /\ pc = [self \in ProcSet |-> CASE self = "audio" -> "Callback"
                                        [] self = "message" -> "Loop"]

Callback == /\ pc["audio"] = "Callback"
            /\ IF callbacks < MaxCallbacks
                  THEN /\ registered
                       /\ inside' = TRUE
                       /\ pc' = [pc EXCEPT !["audio"] = "Render"]
                  ELSE /\ pc' = [pc EXCEPT !["audio"] = "Done"]
                       /\ UNCHANGED inside
            /\ UNCHANGED << ownerAlive, resourcesAlive, registered, listeners,
                            timer, pending, queued, timerQueued, destroyed,
                            callbacks, messages >>

Render == /\ pc["audio"] = "Render"
          /\ Assert(ownerAlive /\ resourcesAlive,
                    "Failure of assertion at line 26, column 9.")
          /\ pc' = [pc EXCEPT !["audio"] = "Post"]
          /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                          listeners, timer, pending, queued, timerQueued,
                          destroyed, callbacks, messages >>

Post == /\ pc["audio"] = "Post"
        /\ pending' = TRUE
        /\ queued' = TRUE
        /\ pc' = [pc EXCEPT !["audio"] = "Leave"]
        /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                        listeners, timer, timerQueued, destroyed, callbacks,
                        messages >>

Leave == /\ pc["audio"] = "Leave"
         /\ inside' = FALSE
         /\ callbacks' = callbacks + 1
         /\ pc' = [pc EXCEPT !["audio"] = "Callback"]
         /\ UNCHANGED << ownerAlive, resourcesAlive, registered, listeners,
                         timer, pending, queued, timerQueued, destroyed,
                         messages >>

Audio == Callback \/ Render \/ Post \/ Leave

Loop == /\ pc["message"] = "Loop"
        /\ IF messages < MaxMessages
              THEN /\ \/ /\ pc' = [pc EXCEPT !["message"] = "StopTimer"]
                         /\ UNCHANGED <<pending, queued, timerQueued>>
                      \/ /\ IF queued
                               THEN /\ queued' = FALSE
                                    /\ IF pending
                                          THEN /\ pending' = FALSE
                                               /\ pc' = [pc EXCEPT !["message"] = "Handle"]
                                          ELSE /\ pc' = [pc EXCEPT !["message"] = "NextMessage"]
                                               /\ UNCHANGED pending
                                    /\ UNCHANGED timerQueued
                               ELSE /\ IF timerQueued
                                          THEN /\ timerQueued' = FALSE
                                               /\ IF timer
                                                     THEN /\ Assert(ownerAlive /\ resourcesAlive,
                                                                    "Failure of assertion at line 53, column 31.")
                                                     ELSE /\ TRUE
                                               /\ UNCHANGED << pending, queued >>
                                          ELSE /\ IF listeners
                                                     THEN /\ pending' = TRUE
                                                          /\ queued' = TRUE
                                                     ELSE /\ TRUE
                                                          /\ UNCHANGED << pending,
                                                                          queued >>
                                               /\ UNCHANGED timerQueued
                                    /\ pc' = [pc EXCEPT !["message"] = "NextMessage"]
              ELSE /\ pc' = [pc EXCEPT !["message"] = "StopTimer"]
                   /\ UNCHANGED << pending, queued, timerQueued >>
        /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                        listeners, timer, destroyed, callbacks, messages >>

Handle == /\ pc["message"] = "Handle"
          /\ Assert(ownerAlive /\ resourcesAlive,
                    "Failure of assertion at line 49, column 21.")
          /\ pc' = [pc EXCEPT !["message"] = "NextMessage"]
          /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                          listeners, timer, pending, queued, timerQueued,
                          destroyed, callbacks, messages >>

NextMessage == /\ pc["message"] = "NextMessage"
               /\ messages' = messages + 1
               /\ pc' = [pc EXCEPT !["message"] = "Loop"]
               /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                               listeners, timer, pending, queued, timerQueued,
                               destroyed, callbacks >>

StopTimer == /\ pc["message"] = "StopTimer"
             /\ timer' = FALSE
             /\ pc' = [pc EXCEPT !["message"] = "RemoveCallback"]
             /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                             listeners, pending, queued, timerQueued,
                             destroyed, callbacks, messages >>

RemoveCallback == /\ pc["message"] = "RemoveCallback"
                  /\ ~inside
                  /\ registered' = FALSE
                  /\ pc' = [pc EXCEPT !["message"] = "Stopped"]
                  /\ UNCHANGED << ownerAlive, resourcesAlive, inside,
                                  listeners, timer, pending, queued,
                                  timerQueued, destroyed, callbacks, messages >>

Stopped == /\ pc["message"] = "Stopped"
           /\ pending' = TRUE
           /\ queued' = TRUE
           /\ pc' = [pc EXCEPT !["message"] = "RemoveListeners"]
           /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                           listeners, timer, timerQueued, destroyed, callbacks,
                           messages >>

RemoveListeners == /\ pc["message"] = "RemoveListeners"
                   /\ listeners' = FALSE
                   /\ pc' = [pc EXCEPT !["message"] = "Cancel"]
                   /\ UNCHANGED << ownerAlive, resourcesAlive, registered,
                                   inside, timer, pending, queued, timerQueued,
                                   destroyed, callbacks, messages >>

Cancel == /\ pc["message"] = "Cancel"
          /\ pending' = FALSE
          /\ pc' = [pc EXCEPT !["message"] = "ReleaseResources"]
          /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                          listeners, timer, queued, timerQueued, destroyed,
                          callbacks, messages >>

ReleaseResources == /\ pc["message"] = "ReleaseResources"
                    /\ resourcesAlive' = FALSE
                    /\ pc' = [pc EXCEPT !["message"] = "DestroyOwner"]
                    /\ UNCHANGED << ownerAlive, registered, inside, listeners,
                                    timer, pending, queued, timerQueued,
                                    destroyed, callbacks, messages >>

DestroyOwner == /\ pc["message"] = "DestroyOwner"
                /\ ownerAlive' = FALSE
                /\ destroyed' = TRUE
                /\ pc' = [pc EXCEPT !["message"] = "LateDispatch"]
                /\ UNCHANGED << resourcesAlive, registered, inside, listeners,
                                timer, pending, queued, timerQueued, callbacks,
                                messages >>

LateDispatch == /\ pc["message"] = "LateDispatch"
                /\ IF queued
                      THEN /\ queued' = FALSE
                           /\ IF pending
                                 THEN /\ pending' = FALSE
                                      /\ Assert(ownerAlive /\ resourcesAlive,
                                                "Failure of assertion at line 89, column 13.")
                                 ELSE /\ TRUE
                                      /\ UNCHANGED pending
                      ELSE /\ TRUE
                           /\ UNCHANGED << pending, queued >>
                /\ pc' = [pc EXCEPT !["message"] = "LateTimer"]
                /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                                listeners, timer, timerQueued, destroyed,
                                callbacks, messages >>

LateTimer == /\ pc["message"] = "LateTimer"
             /\ IF timerQueued
                   THEN /\ timerQueued' = FALSE
                        /\ IF timer
                              THEN /\ Assert(ownerAlive /\ resourcesAlive,
                                             "Failure of assertion at line 95, column 23.")
                              ELSE /\ TRUE
                   ELSE /\ TRUE
                        /\ UNCHANGED timerQueued
             /\ pc' = [pc EXCEPT !["message"] = "Done"]
             /\ UNCHANGED << ownerAlive, resourcesAlive, registered, inside,
                             listeners, timer, pending, queued, destroyed,
                             callbacks, messages >>

Message == Loop \/ Handle \/ NextMessage \/ StopTimer \/ RemoveCallback
              \/ Stopped \/ RemoveListeners \/ Cancel \/ ReleaseResources
              \/ DestroyOwner \/ LateDispatch \/ LateTimer

(* Allow infinite stuttering to prevent deadlock on termination. *)
Terminating == /\ \A self \in ProcSet: pc[self] = "Done"
               /\ UNCHANGED vars

Next == Audio \/ Message
           \/ Terminating

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Audio)
        /\ WF_vars(Message)

Termination == <>(\A self \in ProcSet: pc[self] = "Done")

\* END TRANSLATION

NoRenderAfterRelease == resourcesAlive \/ ~inside
DetachedAtDestruction == ~destroyed \/ (~registered /\ ~listeners /\ ~timer /\ ~pending)
EventuallyDestroyed == <>destroyed
LateMessagesDrained == <>(destroyed /\ ~queued /\ ~timerQueued)
=============================================================================
