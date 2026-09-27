# Total Annihilation sprite animation cursor

This C++20 component advances a cursor through a parsed GAF sequence with the
16-bit timer behavior used by Total Annihilation 3.1c. Time is measured in the
game's animation tick unit; these routines do not infer a wall-clock
conversion.

## Behaviour

- `initialize` starts a cursor, compares the signed initial frame against the
  uint16 frame count before narrowing it to 16 bits, loads its duration, and
  copies the low byte of the sequence's repeat word as the repeat flag. The
  duration lookup returns zero if a negative input wraps outside the decoded
  frames.
- `tick` performs one tick, changes frame when the timer is below two, wraps
  repeating sequences, and clears the sequence pointer on non-repeating
  completion.
- `advance_elapsed_unbounded` subtracts a signed 16-bit elapsed delta with
  16-bit wrapping and advances while the timer interpreted as signed 16-bit is
  below one. It does nothing for sequences with at most one frame.
- `frame_duration` returns `0xffff` for no sequence and otherwise reads the
  duration low word.
- `current_frame` returns the record of frame `frame_index`, or null when the
  cursor has no sequence. The index is the cursor's unsigned 16-bit frame word
  and is not compared with the frame count; an index outside the decoded
  frames gives null.

`advance_elapsed_unbounded` has no step limit: for a malformed repeating
sequence whose durations never make the signed timer positive it does not
return. `advance_elapsed_bounded` exposes the same
arithmetic with a caller-selected frame-advance limit and reports
`ElapsedStatus::advance_limit`; use it when animation data is untrusted.

```sh
cmake -S src/sim/sprite-animation -B local/build-sprite-animation
cmake --build local/build-sprite-animation
ctest --test-dir local/build-sprite-animation --output-on-failure
```
