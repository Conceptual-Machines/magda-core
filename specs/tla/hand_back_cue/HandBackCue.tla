--------------------------- MODULE HandBackCue ---------------------------
EXTENDS Naturals, TLC
CONSTANT MaxCues

(* PrefetchStream::seek / applyPendingCue / read, in
   magda/engine/io/PrefetchStream.cpp, and ClipStreamFeed::BlockScope.
   A cue generation stands for its immutable {generation,sourceStart} pair.
   farbot publication/acquisition is abstracted as a linearizable register.
   Callback work and disk fill after moveTo are outside this model.
   Audio may sound for a finite number of callbacks, then stays silent so the
   final cue can be applied. We do NOT promise application while sounding. *)
(* --algorithm HandBackCue
variables published = 0, applied = 0, readSinceCheck = FALSE,
          lastApplied = 0, appliedWhileSounding = FALSE;

fair process Pool = "pool"
begin
Seek:
    while published < MaxCues do
        published := published + 1;
    end while;
end process;

fair process Audio = "audio"
variables wasSounding = FALSE, cue = 0, soundsLeft = 2;
begin
Block:
    while TRUE do
        wasSounding := readSinceCheck;
        readSinceCheck := FALSE;
AcquireCue:
        cue := published;
Apply:
        if cue /= applied /\ ~wasSounding then
            lastApplied := applied;
            applied := cue;
            appliedWhileSounding := wasSounding;
        end if;
Render:
        if soundsLeft > 0 then
            either
                readSinceCheck := TRUE;
                soundsLeft := soundsLeft - 1;
            or
                soundsLeft := 0;
            end either;
        end if;
    end while;
end process;
end algorithm; *)
\* BEGIN TRANSLATION (chksum(pcal) = "d6a039a7" /\ chksum(tla) = "efacd923")
VARIABLES published, applied, readSinceCheck, lastApplied,
          appliedWhileSounding, pc, wasSounding, cue, soundsLeft

vars == << published, applied, readSinceCheck, lastApplied,
           appliedWhileSounding, pc, wasSounding, cue, soundsLeft >>

ProcSet == {"pool"} \cup {"audio"}

Init == (* Global variables *)
        /\ published = 0
        /\ applied = 0
        /\ readSinceCheck = FALSE
        /\ lastApplied = 0
        /\ appliedWhileSounding = FALSE
        (* Process Audio *)
        /\ wasSounding = FALSE
        /\ cue = 0
        /\ soundsLeft = 2
        /\ pc = [self \in ProcSet |-> CASE self = "pool" -> "Seek"
                                        [] self = "audio" -> "Block"]

Seek == /\ pc["pool"] = "Seek"
        /\ IF published < MaxCues
              THEN /\ published' = published + 1
                   /\ pc' = [pc EXCEPT !["pool"] = "Seek"]
              ELSE /\ pc' = [pc EXCEPT !["pool"] = "Done"]
                   /\ UNCHANGED published
        /\ UNCHANGED << applied, readSinceCheck, lastApplied,
                        appliedWhileSounding, wasSounding, cue, soundsLeft >>

Pool == Seek

Block == /\ pc["audio"] = "Block"
         /\ wasSounding' = readSinceCheck
         /\ readSinceCheck' = FALSE
         /\ pc' = [pc EXCEPT !["audio"] = "AcquireCue"]
         /\ UNCHANGED << published, applied, lastApplied, appliedWhileSounding,
                         cue, soundsLeft >>

AcquireCue == /\ pc["audio"] = "AcquireCue"
              /\ cue' = published
              /\ pc' = [pc EXCEPT !["audio"] = "Apply"]
              /\ UNCHANGED << published, applied, readSinceCheck, lastApplied,
                              appliedWhileSounding, wasSounding, soundsLeft >>

Apply == /\ pc["audio"] = "Apply"
         /\ IF cue /= applied /\ ~wasSounding
               THEN /\ lastApplied' = applied
                    /\ applied' = cue
                    /\ appliedWhileSounding' = wasSounding
               ELSE /\ TRUE
                    /\ UNCHANGED << applied, lastApplied, appliedWhileSounding >>
         /\ pc' = [pc EXCEPT !["audio"] = "Render"]
         /\ UNCHANGED << published, readSinceCheck, wasSounding, cue,
                         soundsLeft >>

Render == /\ pc["audio"] = "Render"
          /\ IF soundsLeft > 0
                THEN /\ \/ /\ readSinceCheck' = TRUE
                           /\ soundsLeft' = soundsLeft - 1
                        \/ /\ soundsLeft' = 0
                           /\ UNCHANGED readSinceCheck
                ELSE /\ TRUE
                     /\ UNCHANGED << readSinceCheck, soundsLeft >>
          /\ pc' = [pc EXCEPT !["audio"] = "Block"]
          /\ UNCHANGED << published, applied, lastApplied,
                          appliedWhileSounding, wasSounding, cue >>

Audio == Block \/ AcquireCue \/ Apply \/ Render

Next == Pool \/ Audio

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Pool)
        /\ WF_vars(Audio)

\* END TRANSLATION

\* Translation inserted by pcal.trans.

Monotonic == lastApplied <= applied /\ applied <= published
SoundingPreserved == ~appliedWhileSounding
FinalCueApplied == <>[](applied = MaxCues)
=============================================================================
