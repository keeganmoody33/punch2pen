# Vocal fixtures for engine golden-file tests

This folder is how Punch2Pen gets an honest transcription check **without Logic**.
The engine still needs **real vocals**. A sine wave, a metronome, or a mixed
beat is not a fixture.

Checked-in audio is **not** shipped. Drop your own file locally. Commit it
only if you own the recording.

## What to drop in

| File | Required | What it is |
|---|---|---|
| `dry-vocal.wav` | Yes, for the vocals job to run instead of skip | 5–12 seconds of **your** dry vocal or spoken take, PCM WAV |
| `expected-words.txt` | Yes, with the WAV | One line: the words you actually said, lowercase is fine |

Copy the example expected file:

```bash
cp fixtures/vocals/expected-words.txt.example fixtures/vocals/expected-words.txt
# then edit it to match what you recorded
```

## Recording recipe (M-series MacBook Pro)

1. In Voice Memos or Logic, record **one original sentence** you made up. No
   commercial song, no cover, no radio/YouTube rip, no beat under the vocal.
2. Bounce or export **16-bit PCM WAV** (AIFF is ok if you convert; not MP3/M4A).
   Mono preferred. 44.1 kHz or 48 kHz.
3. Save as `fixtures/vocals/dry-vocal.wav`.
4. Put the exact words in `fixtures/vocals/expected-words.txt`.

Suggested original line to speak (not a song):

> punch two pen writes the words I sing on the beat

A spoken count (`one two three four five six seven eight nine ten`) is an
easier first fixture for whisper `base`. Still use **your** voice.

Keep the take **dry**: close mic, no instrumental, no Auto-Tune chain. Music
under the vocal makes whisper invent words and the golden file looks “flaky”
when the product is fine.

## What this is not

- Not a Logic punch-in. The plugin only captures while the DAW reports
  recording. That loop stays on your Mac.
- Not a license to check in other people’s tracks.
- Not TTS. A synthetic voice can be a later extra; it does not replace the
  studio take.

## Make CI transcribe (optional)

WAV files are gitignored so a song does not land in the repo by accident.
If `dry-vocal.wav` is **your** original recording:

```bash
git add -f fixtures/vocals/dry-vocal.wav fixtures/vocals/expected-words.txt
```

Until that file exists, `scripts/verify_engine.py vocals` prints **SKIP** and
exits 0. Pass `--require` (or `./scripts/engine_smoke.sh --require-vocals`)
when you want a missing fixture to fail.

## Run

Engine must already be up, **or** use the wrapper which starts an isolated one:

```bash
./scripts/engine_smoke.sh
# or, engine already on 127.0.0.1:7483:
python3 scripts/verify_engine.py vocals
```
