------------------------- MODULE HandBackStandby -------------------------
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS MaxRounds, MaxBlocks, Keys
Ids == 1..MaxRounds

(* ClipVoicePool::{prepareStandby,settleStandby,service},
   ClipVoice::{adoptStandby,render}, StandbyStretcher::claim and
   ClipStreamFeed::BlockScope / publish, in magda/engine/clip/.
   0 is the initial stretcher (outside this allocation model).
   Keys abstract equality of the COMPLETE StretchPrimeKey, not just its beat.
   A round may prepare any key or cancel. A callback may arrive before it.
   Two sequential starts compete for one published standby in each block.
   Feed acquire/release is abstracted as a block pin; publication waits for it.
   We track the minimum owners: pool and table. Extra Reader copies can only
   delay destruction. Object IDs are never reused. Setup is held constant. *)
(* --algorithm HandBackStandby
variables
    poolActive = 0, poolStandby = 0, tableActive = 0, tableStandby = 0,
    pinned = FALSE, allocated = {}, freed = {},
    key = [s \in Ids |-> 0], claimed = [s \in Ids |-> FALSE],
    takes = [s \in Ids |-> 0], withdrawals = [s \in Ids |-> 0];

define
    Live(s) == s = 0 \/ (s \in allocated /\ s \notin freed)
    Owners == {poolActive, poolStandby, tableActive, tableStandby} \ {0}
end define;

fair process Pool = "pool"
variables round = 1, old = 0, won = FALSE;
begin
Round:
    while round <= MaxRounds do
        old := poolStandby;
Withdraw:
        if old /= 0 then
            assert Live(old);
            won := ~claimed[old];
            claimed[old] := TRUE;
            if won then withdrawals[old] := withdrawals[old] + 1; end if;
Promote:
            if ~won then poolActive := old; end if;
Drop:
            poolStandby := 0;
        end if;
Prime:
        either
            skip; \* cancellation / no predicted start / unavailable priming
        or
            with k \in Keys do key[round] := k; end with;
            allocated := allocated \union {round};
            poolStandby := round;
        end either;
Publish:
        await ~pinned;
        tableActive := poolActive;
        tableStandby := poolStandby;
Collect:
        freed := freed \union (allocated \ Owners);
        round := round + 1;
    end while;
end process;

fair process Audio = "audio"
variables block = 0, voice = 0, candidate = 0, active = 0, wanted = 0, used = 0;
begin
Block:
    while block < MaxBlocks do
Acquire:
        pinned := TRUE;
        candidate := tableStandby;
        active := tableActive;
        voice := 0;
        with k \in Keys do wanted := k; end with;
Start:
        while voice < 2 do
            used := active;
            either
                skip; \* track still held: no read and no adoption
            or
                if candidate /= 0 then
                    assert Live(candidate);
                    if key[candidate] = wanted then
Claim:
                        if ~claimed[candidate] then
                            claimed[candidate] := TRUE;
                            takes[candidate] := takes[candidate] + 1;
                            used := candidate;
                        end if;
                    end if;
                end if;
Render:
                assert Live(used);
                if used = candidate /\ candidate /= 0 then
                    assert key[used] = wanted;
                end if;
            end either;
NextVoice:
            voice := voice + 1;
        end while;
Release:
        pinned := FALSE;
        block := block + 1;
    end while;
end process;
end algorithm; *)
\* BEGIN TRANSLATION (chksum(pcal) = "204c210a" /\ chksum(tla) = "391d121e")
VARIABLES poolActive, poolStandby, tableActive, tableStandby, pinned,
          allocated, freed, key, claimed, takes, withdrawals, pc

(* define statement *)
Live(s) == s = 0 \/ (s \in allocated /\ s \notin freed)
Owners == {poolActive, poolStandby, tableActive, tableStandby} \ {0}

VARIABLES round, old, won, block, voice, candidate, active, wanted, used

vars == << poolActive, poolStandby, tableActive, tableStandby, pinned,
           allocated, freed, key, claimed, takes, withdrawals, pc, round, old,
           won, block, voice, candidate, active, wanted, used >>

ProcSet == {"pool"} \cup {"audio"}

Init == (* Global variables *)
        /\ poolActive = 0
        /\ poolStandby = 0
        /\ tableActive = 0
        /\ tableStandby = 0
        /\ pinned = FALSE
        /\ allocated = {}
        /\ freed = {}
        /\ key = [s \in Ids |-> 0]
        /\ claimed = [s \in Ids |-> FALSE]
        /\ takes = [s \in Ids |-> 0]
        /\ withdrawals = [s \in Ids |-> 0]
        (* Process Pool *)
        /\ round = 1
        /\ old = 0
        /\ won = FALSE
        (* Process Audio *)
        /\ block = 0
        /\ voice = 0
        /\ candidate = 0
        /\ active = 0
        /\ wanted = 0
        /\ used = 0
        /\ pc = [self \in ProcSet |-> CASE self = "pool" -> "Round"
                                        [] self = "audio" -> "Block"]

Round == /\ pc["pool"] = "Round"
         /\ IF round <= MaxRounds
               THEN /\ old' = poolStandby
                    /\ pc' = [pc EXCEPT !["pool"] = "Withdraw"]
               ELSE /\ pc' = [pc EXCEPT !["pool"] = "Done"]
                    /\ old' = old
         /\ UNCHANGED << poolActive, poolStandby, tableActive, tableStandby,
                         pinned, allocated, freed, key, claimed, takes,
                         withdrawals, round, won, block, voice, candidate,
                         active, wanted, used >>

Withdraw == /\ pc["pool"] = "Withdraw"
            /\ IF old /= 0
                  THEN /\ Assert(Live(old),
                                 "Failure of assertion at line 36, column 13.")
                       /\ won' = ~claimed[old]
                       /\ claimed' = [claimed EXCEPT ![old] = TRUE]
                       /\ IF won'
                             THEN /\ withdrawals' = [withdrawals EXCEPT ![old] = withdrawals[old] + 1]
                             ELSE /\ TRUE
                                  /\ UNCHANGED withdrawals
                       /\ pc' = [pc EXCEPT !["pool"] = "Promote"]
                  ELSE /\ pc' = [pc EXCEPT !["pool"] = "Prime"]
                       /\ UNCHANGED << claimed, withdrawals, won >>
            /\ UNCHANGED << poolActive, poolStandby, tableActive, tableStandby,
                            pinned, allocated, freed, key, takes, round, old,
                            block, voice, candidate, active, wanted, used >>

Promote == /\ pc["pool"] = "Promote"
           /\ IF ~won
                 THEN /\ poolActive' = old
                 ELSE /\ TRUE
                      /\ UNCHANGED poolActive
           /\ pc' = [pc EXCEPT !["pool"] = "Drop"]
           /\ UNCHANGED << poolStandby, tableActive, tableStandby, pinned,
                           allocated, freed, key, claimed, takes, withdrawals,
                           round, old, won, block, voice, candidate, active,
                           wanted, used >>

Drop == /\ pc["pool"] = "Drop"
        /\ poolStandby' = 0
        /\ pc' = [pc EXCEPT !["pool"] = "Prime"]
        /\ UNCHANGED << poolActive, tableActive, tableStandby, pinned,
                        allocated, freed, key, claimed, takes, withdrawals,
                        round, old, won, block, voice, candidate, active,
                        wanted, used >>

Prime == /\ pc["pool"] = "Prime"
         /\ \/ /\ TRUE
               /\ UNCHANGED <<poolStandby, allocated, key>>
            \/ /\ \E k \in Keys:
                    key' = [key EXCEPT ![round] = k]
               /\ allocated' = (allocated \union {round})
               /\ poolStandby' = round
         /\ pc' = [pc EXCEPT !["pool"] = "Publish"]
         /\ UNCHANGED << poolActive, tableActive, tableStandby, pinned, freed,
                         claimed, takes, withdrawals, round, old, won, block,
                         voice, candidate, active, wanted, used >>

Publish == /\ pc["pool"] = "Publish"
           /\ ~pinned
           /\ tableActive' = poolActive
           /\ tableStandby' = poolStandby
           /\ pc' = [pc EXCEPT !["pool"] = "Collect"]
           /\ UNCHANGED << poolActive, poolStandby, pinned, allocated, freed,
                           key, claimed, takes, withdrawals, round, old, won,
                           block, voice, candidate, active, wanted, used >>

Collect == /\ pc["pool"] = "Collect"
           /\ freed' = (freed \union (allocated \ Owners))
           /\ round' = round + 1
           /\ pc' = [pc EXCEPT !["pool"] = "Round"]
           /\ UNCHANGED << poolActive, poolStandby, tableActive, tableStandby,
                           pinned, allocated, key, claimed, takes, withdrawals,
                           old, won, block, voice, candidate, active, wanted,
                           used >>

Pool == Round \/ Withdraw \/ Promote \/ Drop \/ Prime \/ Publish \/ Collect

Block == /\ pc["audio"] = "Block"
         /\ IF block < MaxBlocks
               THEN /\ pc' = [pc EXCEPT !["audio"] = "Acquire"]
               ELSE /\ pc' = [pc EXCEPT !["audio"] = "Done"]
         /\ UNCHANGED << poolActive, poolStandby, tableActive, tableStandby,
                         pinned, allocated, freed, key, claimed, takes,
                         withdrawals, round, old, won, block, voice, candidate,
                         active, wanted, used >>

Acquire == /\ pc["audio"] = "Acquire"
           /\ pinned' = TRUE
           /\ candidate' = tableStandby
           /\ active' = tableActive
           /\ voice' = 0
           /\ \E k \in Keys:
                wanted' = k
           /\ pc' = [pc EXCEPT !["audio"] = "Start"]
           /\ UNCHANGED << poolActive, poolStandby, tableActive, tableStandby,
                           allocated, freed, key, claimed, takes, withdrawals,
                           round, old, won, block, used >>

Start == /\ pc["audio"] = "Start"
         /\ IF voice < 2
               THEN /\ used' = active
                    /\ \/ /\ TRUE
                          /\ pc' = [pc EXCEPT !["audio"] = "NextVoice"]
                       \/ /\ IF candidate /= 0
                                THEN /\ Assert(Live(candidate),
                                               "Failure of assertion at line 81, column 21.")
                                     /\ IF key[candidate] = wanted
                                           THEN /\ pc' = [pc EXCEPT !["audio"] = "Claim"]
                                           ELSE /\ pc' = [pc EXCEPT !["audio"] = "Render"]
                                ELSE /\ pc' = [pc EXCEPT !["audio"] = "Render"]
               ELSE /\ pc' = [pc EXCEPT !["audio"] = "Release"]
                    /\ used' = used
         /\ UNCHANGED << poolActive, poolStandby, tableActive, tableStandby,
                         pinned, allocated, freed, key, claimed, takes,
                         withdrawals, round, old, won, block, voice, candidate,
                         active, wanted >>

NextVoice == /\ pc["audio"] = "NextVoice"
             /\ voice' = voice + 1
             /\ pc' = [pc EXCEPT !["audio"] = "Start"]
             /\ UNCHANGED << poolActive, poolStandby, tableActive,
                             tableStandby, pinned, allocated, freed, key,
                             claimed, takes, withdrawals, round, old, won,
                             block, candidate, active, wanted, used >>

Claim == /\ pc["audio"] = "Claim"
         /\ IF ~claimed[candidate]
               THEN /\ claimed' = [claimed EXCEPT ![candidate] = TRUE]
                    /\ takes' = [takes EXCEPT ![candidate] = takes[candidate] + 1]
                    /\ used' = candidate
               ELSE /\ TRUE
                    /\ UNCHANGED << claimed, takes, used >>
         /\ pc' = [pc EXCEPT !["audio"] = "Render"]
         /\ UNCHANGED << poolActive, poolStandby, tableActive, tableStandby,
                         pinned, allocated, freed, key, withdrawals, round,
                         old, won, block, voice, candidate, active, wanted >>

Render == /\ pc["audio"] = "Render"
          /\ Assert(Live(used),
                    "Failure of assertion at line 92, column 17.")
          /\ IF used = candidate /\ candidate /= 0
                THEN /\ Assert(key[used] = wanted,
                               "Failure of assertion at line 94, column 21.")
                ELSE /\ TRUE
          /\ pc' = [pc EXCEPT !["audio"] = "NextVoice"]
          /\ UNCHANGED << poolActive, poolStandby, tableActive, tableStandby,
                          pinned, allocated, freed, key, claimed, takes,
                          withdrawals, round, old, won, block, voice,
                          candidate, active, wanted, used >>

Release == /\ pc["audio"] = "Release"
           /\ pinned' = FALSE
           /\ block' = block + 1
           /\ pc' = [pc EXCEPT !["audio"] = "Block"]
           /\ UNCHANGED << poolActive, poolStandby, tableActive, tableStandby,
                           allocated, freed, key, claimed, takes, withdrawals,
                           round, old, won, voice, candidate, active, wanted,
                           used >>

Audio == Block \/ Acquire \/ Start \/ NextVoice \/ Claim \/ Render
            \/ Release

(* Allow infinite stuttering to prevent deadlock on termination. *)
Terminating == /\ \A self \in ProcSet: pc[self] = "Done"
               /\ UNCHANGED vars

Next == Pool \/ Audio
           \/ Terminating

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Pool)
        /\ WF_vars(Audio)

Termination == <>(\A self \in ProcSet: pc[self] = "Done")

\* END TRANSLATION

\* Translation inserted by pcal.trans.

ExclusiveClaim == \A s \in Ids : takes[s] + withdrawals[s] <= 1
OwnersLive == Owners \intersect freed = {}
RetiredFreed == <>[]((allocated \ Owners) \subseteq freed)
Completed == <>(pc["pool"] = "Done" /\ pc["audio"] = "Done")
=============================================================================
