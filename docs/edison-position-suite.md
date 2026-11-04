# Edison position suite pilot

Date: 2026-11-04. Source: one commentated finals match by IMNerd (Frazier Smith), Diva Hero
versus Vayu Turbo. Records:
[positions.json](../tests/edison/reports/position-suite-pilot-2026-11-04/positions.json).

## Result

**The method works for finding and describing positions. They cannot be loaded into the simulator yet.**
About 85 video frames in roughly 15 minutes produced 8 positions (3 high confidence, 5 medium), 1 rejected
position, and 3 leads left unreviewed. This is one video, so these numbers may not carry over to others.

| Position | Actor | Played | Commentator | Confidence |
| --- | --- | --- | --- | --- |
| G1 Trap Dustshoot pick | Vayu | Stratos | Three good options | High |
| G1 Gold Sarcophagus pick | Diva Hero | Reinforcement of the Army | Raises Future Fusion | Medium |
| G1 set Torrential into suspected Ryko | Diva Hero | Set it, no summon | Skeptical; Ryko then hits it | High |
| G1 Heavy Storm into four sets | Diva Hero | Heavy Storm | Describes it meeting Solemn | Medium |
| G3 Trap Dustshoot pick | Vayu | Dark Grepher | Approves; a viewer prefers Malicious | High |
| G3 line after Pulling the Rug | Diva Hero | Mind Control into Absolute Zero | Approves | Medium |
| G3 hold Bottomless against Stratos | Vayu | Holds it | Unclear | Medium |
| G3 set Dark Grepher in Main Phase 2 | Diva Hero | Sets it, keeps Alius | Unsure, then suggests a reason | Medium |

## How it was done

Video opened in the user's Chrome, muted, using the Claude in Chrome tool. A 30-second scan across all
23 minutes found the games and candidate decisions. Each candidate was then stepped through at 5-second
intervals to read the board, duel log and captions together.

- **Readable:** life points, field with ATK/DEF, deck and Graveyard counts, duel log, both hands and face-down
  cards. The replay view makes set cards semi-transparent.
- **Not available:** the transcript panel never finished loading, across two attempts of about 20 seconds.
  Captions were read frame by frame, so sentences between frames can be missing. This limited two positions.
- **Unreliable:** auto-captions mangle card names (Ryko as "Reicho", Reinforcement of the Army as "Roda"). Every
  name was settled from the board or log. Hands at the screen edge are partly cropped, and captions cover some of them.

Face-down cards being visible is useful but also a hazard. Each position separates `actor_knows`, which an
engine may see, from `replay_only`, which it must never see. Set-card identities were read from artwork, so one
position carries an explicit note to confirm it.

## Limits

- **Opinion, not ground truth.** A comment is one human view. The commentator and a viewer disagree on the G3 Dustshoot pick.
  Outcomes in `what_happened` are one sampled future, and a good play can still lose.
- **Not all decisions have enumerated options.** Three of the eight list exact choices. The others are open-ended
  plays or depend on a decklist not captured here.
- **Rejected rather than guessed.** The G2 Stardust Dragon sequence could not be reconstructed, because Dimensional
  Prison banishes rather than destroys and so should not qualify for Stardust's effect.
- **Not tested on any engine.** [The forward-model study](edison-forward-model.md) found that positions built through
  the core's API offer the right actions only 8% of the time, because it cannot inject turn state and
  once-per-turn usage. So these records cannot yet run automatically.
- **Human check still needed.** Someone who knows the format should review these before they count as a benchmark.

## Next uses

The two high-value decision types here are Trap Dustshoot picks, which enumerate cleanly, and whether to expose a
set card to a suspected Ryko. Use these to judge whether the network and search reason about hidden cards at all.
Automatic testing needs a position loader. Until then the records serve as human-readable review cases.

Other candidates from the same channel appeared alongside this video, such as Hero Frogs versus Vayu Turbo and
Gladiator Beast versus Fairies. E3 Yugioh's channel has not been located.
