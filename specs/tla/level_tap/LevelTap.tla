---------------------------- MODULE LevelTap ----------------------------
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS MaxWrites, MaxPeak

Ids == 1..MaxWrites
Peaks == 1..MaxPeak

(* LevelTap::accumulate/read in magda/sdk/tap/LevelTap.hpp (magda-sdk).

   `slot` is one atomic<float> channel. The writer's compare-and-swap and the
   reader's exchange are the only atomic operations. A peak lower than the
   value the writer loads is linearized at that load: the greater value already
   in the slot represents it. `covered` is ghost state naming the writes the
   slot represents, and `receipts` remembers which destructive read reported
   them. Neither exists in the C++.

   The contract gives a tap one writer and one reader. clear() is excluded: it
   runs only after the plan swap has removed the writer. Float arithmetic and
   memory-order weakening are outside the model; peak magnitudes are a small
   ordered set and each atomic operation is linearizable. A weak-CAS spurious
   failure changes no model state; writer fairness permits finite retries. *)
(* --algorithm LevelTap
variables slot = 0,
          peaks = [i \in Ids |-> 0],
          linearized = {},
          covered = {},
          receipts = {};

fair process Writer = "audio"
variables nextWrite = 1, peak = 0, seen = 0;
begin
Write:
    while nextWrite <= MaxWrites do
Pick:
        with value \in Peaks do
            peak := value;
            peaks[nextWrite] := value;
        end with;
Load:   \* accumulate(): initial relaxed load
        seen := slot;
        if peak <= slot then
            \* The slot's greater peak covers this write. Linearize at load.
            covered := covered \union {nextWrite};
            linearized := linearized \union {nextWrite};
            nextWrite := nextWrite + 1;
            goto Write;
        end if;
Cas:    \* compare_exchange_weak; failure refreshes `current`
        if slot = seen then
            slot := peak;
            covered := covered \union {nextWrite};
            linearized := linearized \union {nextWrite};
            nextWrite := nextWrite + 1;
            goto Write;
        else
            seen := slot;
            if peak <= slot then
                covered := covered \union {nextWrite};
                linearized := linearized \union {nextWrite};
                nextWrite := nextWrite + 1;
                goto Write;
            else
                goto Cas;
            end if;
        end if;
    end while;
end process;

fair process Reader = "message"
variables returned = 0;
begin
Read:
    while TRUE do
Exchange:   \* read(): exchange(0)
        returned := slot;
        receipts := receipts \union { <<write, slot>> : write \in covered };
        covered := {};
        slot := 0;
    end while;
end process;
end algorithm; *)
\* BEGIN TRANSLATION (chksum(pcal) = "b2052ae3" /\ chksum(tla) = "e1c46abc")
VARIABLES slot, peaks, linearized, covered, receipts, pc, nextWrite, peak,
          seen, returned

vars == << slot, peaks, linearized, covered, receipts, pc, nextWrite, peak,
           seen, returned >>

ProcSet == {"audio"} \cup {"message"}

Init == (* Global variables *)
        /\ slot = 0
        /\ peaks = [i \in Ids |-> 0]
        /\ linearized = {}
        /\ covered = {}
        /\ receipts = {}
        (* Process Writer *)
        /\ nextWrite = 1
        /\ peak = 0
        /\ seen = 0
        (* Process Reader *)
        /\ returned = 0
        /\ pc = [self \in ProcSet |-> CASE self = "audio" -> "Write"
                                        [] self = "message" -> "Read"]

Write == /\ pc["audio"] = "Write"
         /\ IF nextWrite <= MaxWrites
               THEN /\ pc' = [pc EXCEPT !["audio"] = "Pick"]
               ELSE /\ pc' = [pc EXCEPT !["audio"] = "Done"]
         /\ UNCHANGED << slot, peaks, linearized, covered, receipts, nextWrite,
                         peak, seen, returned >>

Pick == /\ pc["audio"] = "Pick"
        /\ \E value \in Peaks:
             /\ peak' = value
             /\ peaks' = [peaks EXCEPT ![nextWrite] = value]
        /\ pc' = [pc EXCEPT !["audio"] = "Load"]
        /\ UNCHANGED << slot, linearized, covered, receipts, nextWrite, seen,
                        returned >>

Load == /\ pc["audio"] = "Load"
        /\ seen' = slot
        /\ IF peak <= slot
              THEN /\ covered' = (covered \union {nextWrite})
                   /\ linearized' = (linearized \union {nextWrite})
                   /\ nextWrite' = nextWrite + 1
                   /\ pc' = [pc EXCEPT !["audio"] = "Write"]
              ELSE /\ pc' = [pc EXCEPT !["audio"] = "Cas"]
                   /\ UNCHANGED << linearized, covered, nextWrite >>
        /\ UNCHANGED << slot, peaks, receipts, peak, returned >>

Cas == /\ pc["audio"] = "Cas"
       /\ IF slot = seen
             THEN /\ slot' = peak
                  /\ covered' = (covered \union {nextWrite})
                  /\ linearized' = (linearized \union {nextWrite})
                  /\ nextWrite' = nextWrite + 1
                  /\ pc' = [pc EXCEPT !["audio"] = "Write"]
                  /\ seen' = seen
             ELSE /\ seen' = slot
                  /\ IF peak <= slot
                        THEN /\ covered' = (covered \union {nextWrite})
                             /\ linearized' = (linearized \union {nextWrite})
                             /\ nextWrite' = nextWrite + 1
                             /\ pc' = [pc EXCEPT !["audio"] = "Write"]
                        ELSE /\ pc' = [pc EXCEPT !["audio"] = "Cas"]
                             /\ UNCHANGED << linearized, covered, nextWrite >>
                  /\ slot' = slot
       /\ UNCHANGED << peaks, receipts, peak, returned >>

Writer == Write \/ Pick \/ Load \/ Cas

Read == /\ pc["message"] = "Read"
        /\ pc' = [pc EXCEPT !["message"] = "Exchange"]
        /\ UNCHANGED << slot, peaks, linearized, covered, receipts, nextWrite,
                        peak, seen, returned >>

Exchange == /\ pc["message"] = "Exchange"
            /\ returned' = slot
            /\ receipts' = (receipts \union { <<write, slot>> : write \in covered })
            /\ covered' = {}
            /\ slot' = 0
            /\ pc' = [pc EXCEPT !["message"] = "Read"]
            /\ UNCHANGED << peaks, linearized, nextWrite, peak, seen >>

Reader == Read \/ Exchange

Next == Writer \/ Reader

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Writer)
        /\ WF_vars(Reader)

\* END TRANSLATION

\* Translation inserted by pcal.trans.

Delivered ==
    { writeId \in Ids : \E value \in Peaks : <<writeId, value>> \in receipts }

TypeOK ==
    /\ slot \in 0..MaxPeak
    /\ peaks \in [Ids -> 0..MaxPeak]
    /\ linearized \subseteq Ids
    /\ covered \subseteq Ids
    /\ receipts \subseteq (Ids \X Peaks)
    /\ nextWrite \in 1..(MaxWrites + 1)
    /\ peak \in 0..MaxPeak
    /\ seen \in 0..MaxPeak
    /\ returned \in 0..MaxPeak

\* Every completed write is either waiting in the slot or was reported.
NoLostPeak ==
    /\ linearized = covered \union Delivered
    /\ covered \intersect Delivered = {}

\* The slot is at least as loud as every write it stands for.
SlotDominates ==
    /\ \A writeId \in covered : peaks[writeId] <= slot
    /\ slot = 0 \/ \E writeId \in linearized : peaks[writeId] = slot

\* A read reports each write once, at a value at least as loud as that write.
SoundReceipts ==
    /\ \A report \in receipts : peaks[report[1]] <= report[2]
    /\ \A writeId \in Ids :
           Cardinality({report \in receipts : report[1] = writeId}) <= 1

\* Once writes stop, fair polling eventually reports all of them.
AllWritesReported == <>[](Delivered = Ids)

=============================================================================
