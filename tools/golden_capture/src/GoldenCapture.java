// GoldenCapture.java
//
// Windows-host-only JVM tool (Java 25, run against the desktop javac/java on
// PATH -- NOT built via CMake/WSL). Exercises three game RNG primitives
// directly against `D:\STS_BG_Mod\sts-classes.jar` and dumps golden vectors
// used by the C++ tier-1 RNG tests (see docs/stage-a-design.md SS3.7 and
// docs/stage-a-tasks.md task A0.1). None of this file's *output* is
// decompiled Java or extracted from the jar -- only the numbers the game's
// own bytecode produces when run.
//
// Classes exercised (provenance -- read from the decompiled tree at
// D:\STS_BG_Mod\SlayTheSpireDecompiled before writing this file):
//   - com.badlogic.gdx.math.RandomXS128        (RandomXS128.java, whole file)
//   - com.megacrit.cardcrawl.random.Random      (Random.java, whole file)
//   - com.megacrit.cardcrawl.helpers.SeedHelper (SeedHelper.java, whole file)
//   - java.util.Random + java.util.Collections.shuffle, seeded the way
//     CardGroup.shuffle() seeds it (CardGroup.java:561-567), but fed the
//     battery seed directly per task A0.1 instructions (isolates the JDK
//     LCG + shuffle algorithm from shuffleRng.randomLong()).
//   - Floor/act stream derivation formulas read from
//     AbstractDungeon.java:1747-1751 (floor reseed, happens AFTER floorNum
//     is incremented) and the four act dungeon constructors
//     (Exordium.java:56, TheCity.java:46, TheBeyond.java:44,
//     TheEnding.java:49): mapRng seed = Settings.seed + actNum * mult,
//     where actNum is 1/2/3/4 for Exordium/TheCity/TheBeyond/TheEnding and
//     mult is 1/100/200/300 respectively (two different quantities that the
//     design doc's "actNum x {1,100,200,300}" paraphrases as one formula --
//     confirmed by reading all four constructors, not just Exordium's).
//
// All binary output is big-endian (Java DataOutputStream's native order),
// chosen for no reason other than "pick one and be consistent" -- documented
// in tools/golden_capture/README.md. The C++ reader must match.
//
// Usage: java GoldenCapture <output-dir>   (see regen.ps1)

import com.badlogic.gdx.math.RandomXS128;
import com.megacrit.cardcrawl.helpers.SeedHelper;

import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public class GoldenCapture {

    // ---- seed battery (SS3.7 / A0.1 frozen list) --------------------------
    // {0, 1, -1, INT64_MIN, INT64_MAX, "SPIRE" as base-35, 32 "random" seeds}.
    // The 32 "random" seeds are generated with a fixed, documented
    // java.util.Random seed (42) so the battery -- and therefore every golden
    // file derived from it -- is itself a pure function of nothing but this
    // source file. This is unrelated to any in-game RNG class; it is only
    // how *we* pick which longs go into the battery.
    static final class SeedEntry {
        final String label;
        final long value;
        SeedEntry(String label, long value) { this.label = label; this.value = value; }
    }

    static List<SeedEntry> buildSeedBattery() {
        List<SeedEntry> battery = new ArrayList<>();
        battery.add(new SeedEntry("0", 0L));
        battery.add(new SeedEntry("1", 1L));
        battery.add(new SeedEntry("-1", -1L));
        battery.add(new SeedEntry("MIN", Long.MIN_VALUE));
        battery.add(new SeedEntry("MAX", Long.MAX_VALUE));
        battery.add(new SeedEntry("SPIRE", SeedHelper.getLong("SPIRE")));
        java.util.Random gen = new java.util.Random(42L);
        for (int i = 0; i < 32; i++) {
            battery.add(new SeedEntry("r" + i, gen.nextLong()));
        }
        return battery;
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("usage: java GoldenCapture <output-dir>");
            System.exit(2);
        }
        Path outDir = Path.of(args[0]);
        Files.createDirectories(outDir);

        List<SeedEntry> battery = buildSeedBattery();

        writeSeedBatteryManifest(outDir, battery);
        captureXs128(outDir, battery);
        captureWrapper(outDir, battery);
        captureCounterRestore(outDir, battery);
        captureJdkShuffle(outDir, battery);
        captureFloorDerive(outDir, battery);
        captureSeedHelper(outDir, battery);

        System.out.println("Golden capture complete: " + battery.size() + " seeds -> " + outDir.toAbsolutePath());
    }

    // Not one of the six frozen categories, but a small convenience index
    // (label -> value) so the C++ side and future readers don't have to
    // reverse-engineer labels from filenames. Cheap, deterministic, harmless.
    static void writeSeedBatteryManifest(Path outDir, List<SeedEntry> battery) throws IOException {
        StringBuilder sb = new StringBuilder();
        sb.append("# label\tvalue\n");
        for (SeedEntry e : battery) {
            sb.append(e.label).append('\t').append(e.value).append('\n');
        }
        Files.writeString(outDir.resolve("seed_battery.txt"), sb.toString());
    }

    // ---- 1. xs128_<seed>.bin -----------------------------------------------
    // First 10k RandomXS128.nextLong() per battery seed, raw int64 BE.
    static void captureXs128(Path outDir, List<SeedEntry> battery) throws IOException {
        final int N = 10_000;
        for (SeedEntry e : battery) {
            RandomXS128 rng = new RandomXS128(e.value);
            try (DataOutputStream out = openBin(outDir, "xs128_" + e.label + ".bin")) {
                for (int i = 0; i < N; i++) {
                    out.writeLong(rng.nextLong());
                }
            }
        }
    }

    // ---- 2. wrapper_<seed>.bin ---------------------------------------------
    // 1k draws of each of the 10 com.megacrit.cardcrawl.random.Random
    // methods/overloads (Random.java, whole file), one fresh stream per
    // method so each block is independently replayable. File layout per
    // method block: header {method_id:i32, arg0:i64, arg1:i64} then 1000
    // records {counter_after:i32, result_bits:i64}. See README for the
    // method-id table and the result-bits encoding per return type.
    static final int NUM_METHODS = 10;

    // arg encoding for float-typed args: raw IEEE-754 bit pattern
    // (Float.floatToRawIntBits), zero-extended into the i64 slot -- these are
    // bit patterns, not values, so zero-extension (not sign-extension) is
    // correct and exact.
    static long f2bits(float f) { return ((long) Float.floatToRawIntBits(f)) & 0xFFFFFFFFL; }

    // Fixed canonical arguments, shared between the wrapper capture and the
    // counter-restore mixed-sequence capture so both exercise identical call
    // shapes.
    static long methodArg0(int methodId) {
        switch (methodId) {
            case 0: return 999;                  // random(int range)
            case 1: return -500;                 // random(int start,end) start
            case 2: return 1_000_000_000_000L;   // random(long range)
            case 3: return -500_000_000_000L;    // random(long start,end) start
            case 6: return f2bits(0.5f);          // randomBoolean(float chance)
            case 8: return f2bits(100.0f);        // random(float range)
            case 9: return f2bits(-50.0f);        // random(float start,end) start
            default: return 0;
        }
    }
    static long methodArg1(int methodId) {
        switch (methodId) {
            case 1: return 500;                   // random(int start,end) end
            case 3: return 500_000_000_000L;      // random(long start,end) end
            case 9: return f2bits(50.0f);          // random(float start,end) end
            default: return 0;
        }
    }

    // Executes wrapper method `methodId` once on `r` and returns the result
    // encoded into a bit-exact i64 slot (see f2bits doc above; ints/longs are
    // widened/copied verbatim, booleans as 0/1).
    static long drawOnce(com.megacrit.cardcrawl.random.Random r, int methodId) {
        switch (methodId) {
            case 0: return r.random((int) methodArg0(0));
            case 1: return r.random((int) methodArg0(1), (int) methodArg1(1));
            case 2: return r.random(methodArg0(2));
            case 3: return r.random(methodArg0(3), methodArg1(3));
            case 4: return r.randomLong();
            case 5: return r.randomBoolean() ? 1L : 0L;
            case 6: return r.randomBoolean(Float.intBitsToFloat((int) methodArg0(6))) ? 1L : 0L;
            case 7: return f2bits(r.random());
            case 8: return f2bits(r.random(Float.intBitsToFloat((int) methodArg0(8))));
            case 9: return f2bits(r.random(Float.intBitsToFloat((int) methodArg0(9)), Float.intBitsToFloat((int) methodArg1(9))));
            default: throw new IllegalArgumentException("bad methodId " + methodId);
        }
    }

    static void captureWrapper(Path outDir, List<SeedEntry> battery) throws IOException {
        final int DRAWS = 1000;
        for (SeedEntry e : battery) {
            try (DataOutputStream out = openBin(outDir, "wrapper_" + e.label + ".bin")) {
                for (int m = 0; m < NUM_METHODS; m++) {
                    com.megacrit.cardcrawl.random.Random r = new com.megacrit.cardcrawl.random.Random(e.value);
                    out.writeInt(m);
                    out.writeLong(methodArg0(m));
                    out.writeLong(methodArg1(m));
                    for (int i = 0; i < DRAWS; i++) {
                        long resultBits = drawOnce(r, m);
                        out.writeInt(r.counter);
                        out.writeLong(resultBits);
                    }
                }
            }
        }
    }

    // ---- 3. counter_restore_<seed>.bin -------------------------------------
    // A fixed 137-call mixed sequence (cycling method ids 0..9) run on a
    // fresh stream, compared against the two documented replay paths:
    //   - `new Random(seed, N)`   -- replays N x random(999)  (Random.java:28)
    //   - fresh Random + setCounter(N) -- replays N x randomBoolean() (Random.java:42)
    // Design doc SS3.2 claims: both replay paths land the underlying
    // RandomXS128 engine in the same (s0,s1) state as N arbitrary mixed
    // single-draw calls, because every wrapper method (bar the ~1e-16
    // rejection case) consumes exactly one nextLong() and nextLong()'s state
    // transition does not depend on how the caller post-processes the value.
    // This file lets the C++ test assert that equality directly instead of
    // just trusting the design doc's reading.
    static final int MIXED_SEQUENCE_N = 137;

    static void captureCounterRestore(Path outDir, List<SeedEntry> battery) throws IOException {
        for (SeedEntry e : battery) {
            com.megacrit.cardcrawl.random.Random direct = new com.megacrit.cardcrawl.random.Random(e.value);
            for (int i = 0; i < MIXED_SEQUENCE_N; i++) {
                drawOnce(direct, i % NUM_METHODS);
            }

            com.megacrit.cardcrawl.random.Random ctorReplay =
                    new com.megacrit.cardcrawl.random.Random(e.value, MIXED_SEQUENCE_N);

            com.megacrit.cardcrawl.random.Random setCounterReplay = new com.megacrit.cardcrawl.random.Random(e.value);
            setCounterReplay.setCounter(MIXED_SEQUENCE_N);

            try (DataOutputStream out = openBin(outDir, "counter_restore_" + e.label + ".bin")) {
                out.writeInt(MIXED_SEQUENCE_N);
                writeStreamState(out, direct);
                writeStreamState(out, ctorReplay);
                writeStreamState(out, setCounterReplay);
            }
        }
    }

    static void writeStreamState(DataOutputStream out, com.megacrit.cardcrawl.random.Random r) throws IOException {
        out.writeLong(r.random.getState(0));
        out.writeLong(r.random.getState(1));
        out.writeInt(r.counter);
    }

    // ---- 4. jdk_shuffle_<seed>_<n>.bin --------------------------------------
    // Collections.shuffle(1..N, new java.util.Random(batterySeed)) -- seeding
    // java.util.Random directly with the battery seed (not with
    // shuffleRng.randomLong()) per task instructions, to test the JDK LCG +
    // shuffle algorithm in isolation. Confirmed against CardGroup.java:561-567
    // that the game always constructs the JDK Random this way (just fed a
    // stream-drawn long in the real game, a battery seed here).
    static final int[] SHUFFLE_NS = {5, 10, 71, 128};

    static void captureJdkShuffle(Path outDir, List<SeedEntry> battery) throws IOException {
        for (SeedEntry e : battery) {
            for (int n : SHUFFLE_NS) {
                java.util.Random jr = new java.util.Random(e.value);
                List<Integer> list = new ArrayList<>(n);
                for (int i = 1; i <= n; i++) list.add(i);
                java.util.Collections.shuffle(list, jr);
                try (DataOutputStream out = openBin(outDir, "jdk_shuffle_" + e.label + "_" + n + ".bin")) {
                    for (int v : list) out.writeInt(v);
                }
            }
        }
    }

    // ---- 5. floor_derive_<seed>.bin -----------------------------------------
    // Per floor 1..55: the 5 floor-scoped streams (monsterHpRng, aiRng,
    // shuffleRng, cardRandomRng, miscRng), each constructed exactly as
    // AbstractDungeon.nextRoomTransition does at AbstractDungeon.java:1747-1751:
    //   new Random(Settings.seed + (long) floorNum)
    // -- i.e. ALL FIVE use the identical seed value for a given floor, so
    // their initial (s0,s1) state is identical at floor entry; they only
    // diverge once combat starts drawing from them in different
    // order/counts. This file captures that pre-draw state and makes the
    // "all five equal at floor entry" fact something a C++ test can assert
    // instead of silently relying on. Then 4 mapRng entries, one per act,
    // using the exact per-class formulas read from Exordium.java:56,
    // TheCity.java:46, TheBeyond.java:44, TheEnding.java:49 (actNum is
    // 1/2/3/4 for act 1/2/3/4, incremented in
    // AbstractDungeon.dungeonTransitionSetup before the act's dungeon
    // constructor runs):
    //   act1 (Exordium):  seed + actNum*1   = seed + 1
    //   act2 (TheCity):   seed + actNum*100 = seed + 200
    //   act3 (TheBeyond): seed + actNum*200 = seed + 600
    //   act4 (TheEnding): seed + actNum*300 = seed + 1200
    static final int MAX_FLOOR = 55;
    static final int NUM_FLOOR_STREAMS = 5; // monsterHpRng, aiRng, shuffleRng, cardRandomRng, miscRng
    static final long[] MAP_ACT_OFFSETS = {1L, 200L, 600L, 1200L}; // act1..act4

    static void captureFloorDerive(Path outDir, List<SeedEntry> battery) throws IOException {
        for (SeedEntry e : battery) {
            try (DataOutputStream out = openBin(outDir, "floor_derive_" + e.label + ".bin")) {
                for (int floor = 1; floor <= MAX_FLOOR; floor++) {
                    long floorSeed = e.value + (long) floor;
                    for (int s = 0; s < NUM_FLOOR_STREAMS; s++) {
                        com.megacrit.cardcrawl.random.Random stream = new com.megacrit.cardcrawl.random.Random(floorSeed);
                        out.writeLong(stream.random.getState(0));
                        out.writeLong(stream.random.getState(1));
                    }
                }
                for (long offset : MAP_ACT_OFFSETS) {
                    com.megacrit.cardcrawl.random.Random mapStream = new com.megacrit.cardcrawl.random.Random(e.value + offset);
                    out.writeLong(mapStream.random.getState(0));
                    out.writeLong(mapStream.random.getState(1));
                }
            }
        }
    }

    // ---- 6. seedhelper.txt ---------------------------------------------------
    // String<->long round trips (SeedHelper.java, whole file) for the whole
    // battery (covers 0, negatives, MIN, MAX) plus one explicit O->0
    // sterilization case: SeedHelper.getLong() upper-cases and replaces "O"
    // with "0" before decoding (SeedHelper.java:78), so a string with an 'O'
    // substituted for a '0' must decode to the same long as the original.
    static void captureSeedHelper(Path outDir, List<SeedEntry> battery) throws IOException {
        StringBuilder sb = new StringBuilder();
        sb.append("# SeedHelper golden vectors (source: SeedHelper.java)\n");
        sb.append("# RT rows: label\\toriginal_long\\tstring\\troundtrip_long\\tmatch(1/0)\n");
        String oTestLabel = null;
        long oTestOriginal = 0;
        String oTestString = null;
        for (SeedEntry e : battery) {
            String s = SeedHelper.getString(e.value);
            long back = SeedHelper.getLong(s);
            sb.append("RT\t").append(e.label).append('\t').append(e.value).append('\t')
              .append(s).append('\t').append(back).append('\t').append(back == e.value ? 1 : 0).append('\n');
            if (oTestString == null && s.indexOf('0') >= 0) {
                oTestLabel = e.label;
                oTestOriginal = e.value;
                oTestString = s;
            }
        }
        if (oTestString != null) {
            String withO = oTestString.replaceFirst("0", "O");
            long viaO = SeedHelper.getLong(withO);
            sb.append("# OTEST row: label\\toriginal_long\\tstring_with_0\\tstring_with_O\\tgetLong(string_with_O)\\tmatch(1/0)\n");
            sb.append("OTEST\t").append(oTestLabel).append('\t').append(oTestOriginal).append('\t')
              .append(oTestString).append('\t').append(withO).append('\t').append(viaO)
              .append('\t').append(viaO == oTestOriginal ? 1 : 0).append('\n');
        }
        // Also exercise getValidCharacter's specific 'O' -> '0' mapping (used
        // interactively by the game's seed-entry UI, not by getLong/getString,
        // but part of "SeedHelper.java, whole file").
        sb.append("# VALIDCHAR rows: input_char\\tmapped_char_or_NULL\n");
        sb.append("VALIDCHAR\tO\t").append(SeedHelper.getValidCharacter("O", "")).append('\n');
        sb.append("VALIDCHAR\to\t").append(SeedHelper.getValidCharacter("o", "")).append('\n');
        sb.append("VALIDCHAR\t0\t").append(SeedHelper.getValidCharacter("0", "")).append('\n');
        String invalid = SeedHelper.getValidCharacter("!", "");
        sb.append("VALIDCHAR\t!\t").append(invalid == null ? "NULL" : invalid).append('\n');

        Files.writeString(outDir.resolve("seedhelper.txt"), sb.toString());
    }

    // ---- io helper -----------------------------------------------------------
    static DataOutputStream openBin(Path outDir, String filename) throws IOException {
        File f = outDir.resolve(filename).toFile();
        return new DataOutputStream(new FileOutputStream(f));
    }
}
