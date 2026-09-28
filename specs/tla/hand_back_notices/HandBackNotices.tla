------------------------- MODULE HandBackNotices -------------------------
EXTENDS Naturals, Sequences, TLC

CONSTANTS Capacity, Tracks, MaxChanges
Values == 0..2
NoRelease == 0

(* ClipVoicePool::announceHandBacks / drainHandBacks, in
   magda/engine/clip/ClipVoicePool.cpp. A value is a release beat, or 0 for
   cancellation (the C++ notice uses NaN). Changes occur between callbacks.
   Labels separate payload writes, release publication and acquire snapshots.
   The serial field is ghost state detecting reuse of an unread ring slot.
   Fixed track membership; no uint64 counter overflow or beat conversion. *)
(* --algorithm HandBackNotices
variables
    desired = [t \in 1..Tracks |-> NoRelease],
    announced = [t \in 1..Tracks |-> NoRelease],
    delivered = [t \in 1..Tracks |-> NoRelease],
    ring = [s \in 0..(Capacity-1) |-> [track |-> 1, value |-> 0, serial |-> 0]],
    written = 0, read = 0, changed = 0;

fair process Audio = "audio"
variables track = 1, w = 0, available = 0;
begin
Block:
    while TRUE do
        if changed < MaxChanges then
            changed := changed + 1;
            with t \in 1..Tracks, v \in Values do desired[t] := v; end with;
        end if;
        track := 1;
Scan:
        while track <= Tracks do
            if desired[track] /= announced[track] then
                w := written;
CapacityCheck:
                available := read;
                if w - available >= Capacity then goto Block; end if;
WritePayload:
                ring[w % Capacity] :=
                    [track |-> track, value |-> desired[track], serial |-> w+1];
Publish:
                written := w + 1;
Remember:
                announced[track] := desired[track];
            end if;
NextTrack:
            track := track + 1;
        end while;
    end while;
end process;

fair process Pool = "pool"
variables limit = 0, cursor = 0, notice = [track |-> 1, value |-> 0, serial |-> 0];
begin
Snapshot:
    while TRUE do
        await written > read;
        limit := written;
        cursor := read;
Drain:
        while cursor < limit do
            notice := ring[cursor % Capacity];
            assert notice.serial = cursor + 1;
Apply:
            delivered[notice.track] := notice.value;
            cursor := cursor + 1;
        end while;
Ack:
        read := cursor;
    end while;
end process;
end algorithm; *)
\* BEGIN TRANSLATION (chksum(pcal) = "c2175658" /\ chksum(tla) = "890dd5c5")
VARIABLES desired, announced, delivered, ring, written, read, changed, pc,
          track, w, available, limit, cursor, notice

vars == << desired, announced, delivered, ring, written, read, changed, pc,
           track, w, available, limit, cursor, notice >>

ProcSet == {"audio"} \cup {"pool"}

Init == (* Global variables *)
        /\ desired = [t \in 1..Tracks |-> NoRelease]
        /\ announced = [t \in 1..Tracks |-> NoRelease]
        /\ delivered = [t \in 1..Tracks |-> NoRelease]
        /\ ring = [s \in 0..(Capacity-1) |-> [track |-> 1, value |-> 0, serial |-> 0]]
        /\ written = 0
        /\ read = 0
        /\ changed = 0
        (* Process Audio *)
        /\ track = 1
        /\ w = 0
        /\ available = 0
        (* Process Pool *)
        /\ limit = 0
        /\ cursor = 0
        /\ notice = [track |-> 1, value |-> 0, serial |-> 0]
        /\ pc = [self \in ProcSet |-> CASE self = "audio" -> "Block"
                                        [] self = "pool" -> "Snapshot"]

Block == /\ pc["audio"] = "Block"
         /\ IF changed < MaxChanges
               THEN /\ changed' = changed + 1
                    /\ \E t \in 1..Tracks:
                         \E v \in Values:
                           desired' = [desired EXCEPT ![t] = v]
               ELSE /\ TRUE
                    /\ UNCHANGED << desired, changed >>
         /\ track' = 1
         /\ pc' = [pc EXCEPT !["audio"] = "Scan"]
         /\ UNCHANGED << announced, delivered, ring, written, read, w,
                         available, limit, cursor, notice >>

Scan == /\ pc["audio"] = "Scan"
        /\ IF track <= Tracks
              THEN /\ IF desired[track] /= announced[track]
                         THEN /\ w' = written
                              /\ pc' = [pc EXCEPT !["audio"] = "CapacityCheck"]
                         ELSE /\ pc' = [pc EXCEPT !["audio"] = "NextTrack"]
                              /\ w' = w
              ELSE /\ pc' = [pc EXCEPT !["audio"] = "Block"]
                   /\ w' = w
        /\ UNCHANGED << desired, announced, delivered, ring, written, read,
                        changed, track, available, limit, cursor, notice >>

NextTrack == /\ pc["audio"] = "NextTrack"
             /\ track' = track + 1
             /\ pc' = [pc EXCEPT !["audio"] = "Scan"]
             /\ UNCHANGED << desired, announced, delivered, ring, written,
                             read, changed, w, available, limit, cursor,
                             notice >>

CapacityCheck == /\ pc["audio"] = "CapacityCheck"
                 /\ available' = read
                 /\ IF w - available' >= Capacity
                       THEN /\ pc' = [pc EXCEPT !["audio"] = "Block"]
                       ELSE /\ pc' = [pc EXCEPT !["audio"] = "WritePayload"]
                 /\ UNCHANGED << desired, announced, delivered, ring, written,
                                 read, changed, track, w, limit, cursor,
                                 notice >>

WritePayload == /\ pc["audio"] = "WritePayload"
                /\ ring' = [ring EXCEPT ![w % Capacity] = [track |-> track, value |-> desired[track], serial |-> w+1]]
                /\ pc' = [pc EXCEPT !["audio"] = "Publish"]
                /\ UNCHANGED << desired, announced, delivered, written, read,
                                changed, track, w, available, limit, cursor,
                                notice >>

Publish == /\ pc["audio"] = "Publish"
           /\ written' = w + 1
           /\ pc' = [pc EXCEPT !["audio"] = "Remember"]
           /\ UNCHANGED << desired, announced, delivered, ring, read, changed,
                           track, w, available, limit, cursor, notice >>

Remember == /\ pc["audio"] = "Remember"
            /\ announced' = [announced EXCEPT ![track] = desired[track]]
            /\ pc' = [pc EXCEPT !["audio"] = "NextTrack"]
            /\ UNCHANGED << desired, delivered, ring, written, read, changed,
                            track, w, available, limit, cursor, notice >>

Audio == Block \/ Scan \/ NextTrack \/ CapacityCheck \/ WritePayload
            \/ Publish \/ Remember

Snapshot == /\ pc["pool"] = "Snapshot"
            /\ written > read
            /\ limit' = written
            /\ cursor' = read
            /\ pc' = [pc EXCEPT !["pool"] = "Drain"]
            /\ UNCHANGED << desired, announced, delivered, ring, written, read,
                            changed, track, w, available, notice >>

Drain == /\ pc["pool"] = "Drain"
         /\ IF cursor < limit
               THEN /\ notice' = ring[cursor % Capacity]
                    /\ Assert(notice'.serial = cursor + 1,
                              "Failure of assertion at line 64, column 13.")
                    /\ pc' = [pc EXCEPT !["pool"] = "Apply"]
               ELSE /\ pc' = [pc EXCEPT !["pool"] = "Ack"]
                    /\ UNCHANGED notice
         /\ UNCHANGED << desired, announced, delivered, ring, written, read,
                         changed, track, w, available, limit, cursor >>

Apply == /\ pc["pool"] = "Apply"
         /\ delivered' = [delivered EXCEPT ![notice.track] = notice.value]
         /\ cursor' = cursor + 1
         /\ pc' = [pc EXCEPT !["pool"] = "Drain"]
         /\ UNCHANGED << desired, announced, ring, written, read, changed,
                         track, w, available, limit, notice >>

Ack == /\ pc["pool"] = "Ack"
       /\ read' = cursor
       /\ pc' = [pc EXCEPT !["pool"] = "Snapshot"]
       /\ UNCHANGED << desired, announced, delivered, ring, written, changed,
                       track, w, available, limit, cursor, notice >>

Pool == Snapshot \/ Drain \/ Apply \/ Ack

Next == Audio \/ Pool

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Audio)
        /\ WF_vars(Pool)

\* END TRANSLATION

\* Translation inserted by pcal.trans.

QueueBounded == read <= written /\ written - read <= Capacity
LatestDelivered == <>[](changed = MaxChanges /\ delivered = desired /\ read = written)
=============================================================================
