# Usability test protocol (five novice testers)

*Brief 8.8. A ready-to-run protocol for a human facilitator. It can't be run by an AI
agent; results go in `docs/usability/YYYY-MM-DD.md`. Run it at the end of M7 (tutorial)
and again before v0.1, on at least two operating systems.*

## Participants

- Five adults with **no CAD experience** (have never used Fusion, FreeCAD, SolidWorks,
  Onshape, SketchUp or Tinkercad beyond 10 minutes). Mix: at least two who own or use a
  3D printer, at least one left-handed mouse user, at least one laptop-trackpad-only
  user (macOS if possible), at least one person over 60, and, if possible, one with
  colour-vision deficiency.
- Screen them with the questionnaire at the end of this file. Compensate them for their
  time.

## Setup

- A clean user account with a freshly installed Jigsmith build (installer from CI
  artifacts), first launch not yet done, no internet needed (unplug to prove it).
- Screen and audio recording (with consent), a stopwatch, and the observation sheet below.
- The facilitator does not help except at the "rescue" points below, and records every
  rescue.

## Tasks

| # | Task (read aloud, give the card) | Success criterion | Time limit |
|---|---|---|---|
| T0 | "Start the program and get ready to make something." | Reaches the welcome screen and picks a start option without help | 2 min |
| T1 (A1) | "Using only what the program shows you, make the phone stand and save a file you could send to a 3D printer." | An STL exists whose bounding box is within 10% of 90 × 70 mm footprint and has a back and lip | **15 min** |
| T2 | "Make the base of the stand 20 mm wider." | Base width changed via parameter or dimension; fillets still present; no feature in error | 4 min |
| T3 | "Round the top edge of the lip a bit more." | Lip fillet radius increased; no error | 3 min |
| T4 (A5) | "Try to round the lip edge with a radius of 20 mm, then get the model back to working." | Reads the explanation and applies the suggestion or undoes; ends with a valid model | 4 min |
| T5 | "Find the tool that makes a part hollow." | Opens the palette or panel and finds Shell, saying its name | 1 min |
| T6 | "Look at the stand from the front, then get back to where you started." | Uses gizmo/views then Home or Fit | 1 min |
| T7 (A6) | Facilitator force-quits the app during T2. "Carry on." | Accepts recovery and finds their work | 2 min |

Rescue points: after the time limit, the facilitator gives the single hint card for that
task (pre-written, below), then records the task as "assisted".

## What to record

- **Per tutorial step**: completion time (from the tutorial's own log, enabled by a
  `--study-log` switch planned for M7), number of errors (wrong tool, undo used, error
  message shown), rescues.
- **Per task**: success / assisted / fail, time, the first thing the participant tried,
  and every moment of visible hesitation over 10 s, with what was on screen.
- Quotes, especially about words they did not understand.
- After the session: SUS questionnaire (10 items) and three questions: "What was the
  most confusing moment?", "What would you have searched for that you couldn't find?",
  "Would you use it again for a real project?"

## Key metrics and targets (v0.1 gate)

| Metric | Target |
|---|---|
| T1 completion without assistance | ≥ 4 of 5 participants within 15 min |
| Tutorial step error rate | ≤ 1 error per 5 steps, median |
| Median tutorial step time | ≤ 40 s |
| T4 recovered without help | ≥ 4 of 5 |
| SUS score | ≥ 70 |

## Hint cards

- T1: "The bar at the top tells you which step you are on and what to do next."
- T2: "Every size has a name. Look in the panel on the left."
- T3: "Steps you took are listed at the bottom. Click the one that rounds the lip."
- T4: "Read the message on the right. It suggests a size that works."
- T5: "There is a search box at the top right."
- T6: "Look at the coloured axes in the top-right corner of the 3D view."

## Screening questionnaire

1. Have you used any 3D design program for more than 10 minutes? (exclude if yes)
2. Do you use a 3D printer, laser cutter or CNC machine? (record)
3. Which computer and pointing device do you normally use? (record; balance the group)
4. Do you have any difficulty telling colours apart? (record)
