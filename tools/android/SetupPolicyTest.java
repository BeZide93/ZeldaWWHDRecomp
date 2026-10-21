import org.wwhdrecomp.wwhd.SetupPolicy;

public final class SetupPolicyTest {
    private static void expect(String expected, String actual) {
        if (!java.util.Objects.equals(expected, actual))
            throw new AssertionError("Expected " + expected + ", got " + actual);
    }
    public static void main(String[] args) {
        SetupPolicy policy = new SetupPolicy();
        expect(null, policy.update(0, 300, 80, false, false));
        // Android thermal status SEVERE pauses; hysteresis holds until LIGHT and <= 41.0 °C
        expect("heat", policy.update(3, 300, 80, true, false));
        expect("heat", policy.update(2, 300, 80, true, false));
        expect("heat", policy.update(1, 415, 80, true, false));
        expect(null, policy.update(1, 410, 80, true, false));
        // battery temperature alone: 42.0-44.9 °C runs while Android reports none/light (issue #142)
        expect(null, policy.update(0, 431, 80, true, false));
        expect(null, policy.update(1, 449, 80, true, false));
        expect("heat", policy.update(0, 450, 80, true, false));
        expect("heat", policy.update(0, 420, 80, true, false));
        expect("heat", policy.update(2, 411, 80, true, false));
        expect(null, policy.update(0, 400, 80, true, false));
        // 42.0 °C together with MODERATE pauses; 41.9 °C with MODERATE does not
        expect(null, policy.update(2, 419, 80, true, false));
        expect("heat", policy.update(2, 420, 80, true, false));
        expect(null, policy.update(0, -1, 80, true, false));
        // battery level
        expect("battery", policy.update(0, 390, 20, false, false));
        expect("battery", policy.update(0, 300, 29, false, false));
        expect(null, policy.update(0, 300, 30, false, false));
        expect("manual", policy.update(3, 450, 10, false, true));
        expect("heat", policy.update(3, 450, 10, true, false));
        expect(null, policy.update(0, 300, 10, true, false));
        expect("battery_unknown", policy.update(0, -1, -1, false, false));
        expect(null, policy.update(0, -1, -1, true, false));

        // Continue anyway: heat override skips heat pauses, never CRITICAL or a manual pause
        SetupPolicy anyway = new SetupPolicy();
        expect("heat", anyway.update(3, 431, 80, true, false, false, false));
        expect(null, anyway.update(3, 431, 80, true, false, true, false));
        expect(null, anyway.update(0, 470, 80, true, false, true, false));
        expect("critical", anyway.update(4, 431, 80, true, false, true, false));
        expect("critical", anyway.update(6, 431, 80, true, false, true, true));
        expect("critical", anyway.update(4, 300, 80, true, false, false, false));
        expect("manual", anyway.update(4, 300, 80, true, true, true, true));
        expect(null, anyway.update(3, 431, 80, true, false, true, false));
        // the heat override does not skip battery pauses; the battery override does
        expect("battery", anyway.update(3, 431, 10, false, false, true, false));
        expect(null, anyway.update(3, 431, 10, false, false, true, true));
        expect("heat", anyway.update(3, 431, 10, false, false, false, true));
        expect(null, anyway.update(0, 300, -1, false, false, false, true));
        expect("battery_unknown", new SetupPolicy().update(0, 300, -1, false, false, false, false));
        // the hysteresis state keeps tracking while overridden
        expect(null, anyway.update(1, 300, 80, true, false, false, false));

        // texts
        expect("none", SetupPolicy.thermalName(0));
        expect("moderate", SetupPolicy.thermalName(2));
        expect("critical", SetupPolicy.thermalName(4));
        expect("shutdown", SetupPolicy.thermalName(6));
        expect("n/a", SetupPolicy.thermalName(-1));
        expect("n/a", SetupPolicy.thermalName(7));
        expect("43.1 °C", SetupPolicy.batteryTemperature(431));
        expect("n/a", SetupPolicy.batteryTemperature(-1));
        expect("n/a", SetupPolicy.batteryTemperature(Integer.MIN_VALUE));
        expect("Waiting for the Android device to cool down (battery 43.1 °C, thermal status: none)",
            SetupPolicy.description("heat", 0, 431));
        expect("Waiting for the Android device to cool down (battery n/a, thermal status: severe)",
            SetupPolicy.description("heat", 3, -1));
        String critical = SetupPolicy.description("critical", 4, 470);
        if (!critical.startsWith("Paused because Android reports the device as critically hot (battery 47.0 °C, thermal status: critical).") ||
            !critical.contains("Continue anyway does not apply"))
            throw new AssertionError(critical);
        expect("Waiting for charging or at least 30% battery", SetupPolicy.description("battery", 0, 300));
        expect("Paused by you", SetupPolicy.description("manual", 4, 470));
        if (!SetupPolicy.overridable("heat") || !SetupPolicy.overridable("battery") ||
            !SetupPolicy.overridable("battery_unknown") || SetupPolicy.overridable("critical") ||
            SetupPolicy.overridable("manual") || SetupPolicy.overridable(null))
            throw new AssertionError("overridable reasons");
        // compile workers: 8 cores and 10 GB free -> 4; memory and heat limit it
        if (SetupPolicy.compileJobs(8, 10L << 30, false, 0) != 4) throw new AssertionError("8 cores, 10 GB");
        if (SetupPolicy.compileJobs(8, (1L << 30) + (1100L << 20), false, 0) != 2) throw new AssertionError("2.1 GB free");
        if (SetupPolicy.compileJobs(8, 512L << 20, false, 0) != 1) throw new AssertionError("little memory");
        if (SetupPolicy.compileJobs(4, 10L << 30, false, 0) != 2) throw new AssertionError("4 cores");
        if (SetupPolicy.compileJobs(8, 10L << 30, true, 0) != 1) throw new AssertionError("low-RAM phone");
        if (SetupPolicy.compileJobs(8, 10L << 30, false, 2) != 1) throw new AssertionError("warm phone");
        expect("Compiling: 30/80 · about 12 min left", SetupPolicy.compileProgress("compiled", 22, 8, 80, 700));
        expect("Compiling: 3/80", SetupPolicy.compileProgress("compiled", 3, 0, 80, -1));
        expect("Compiling: 79/80 · about a minute left", SetupPolicy.compileProgress("compiled", 79, 0, 80, 20));
        expect("Compiling: done (80/80)", SetupPolicy.compileProgress("complete", 80, 0, 80, -1));
        expect(null, SetupPolicy.compileProgress("compiled", 1, 0, 0, -1));

        // Thermal status trust (issue #254): SEVERE needs a 40.0 °C battery or a 90 °C CPU/GPU sensor
        float nan = Float.NaN;
        // AYANEO Pocket EVO (v0.4.0 reports): SEVERE at a 30-31 °C battery, hottest CPU 60.1 / 77.2 °C
        if (!SetupPolicy.thermalStatusUnreliable(3, 300, 60.1f)) throw new AssertionError("EVO 30 °C, CPU 60.1");
        if (!SetupPolicy.thermalStatusUnreliable(3, 310, 77.2f)) throw new AssertionError("EVO 31 °C, CPU 77.2");
        if (!SetupPolicy.thermalStatusUnreliable(3, 300, nan)) throw new AssertionError("EVO, sensors unreadable");
        if (!SetupPolicy.thermalStatusUnreliable(3, 399, 89.9f)) throw new AssertionError("39.9 °C, 89.9 °C");
        if (SetupPolicy.trustedThermal(3, 310, 77.2f) != -1) throw new AssertionError("EVO ignored");
        // real heat: a hot battery or a hot CPU/GPU backs SEVERE up
        if (SetupPolicy.thermalStatusUnreliable(3, 440, nan)) throw new AssertionError("hot device 44 °C");
        if (SetupPolicy.thermalStatusUnreliable(3, 440, 85f)) throw new AssertionError("hot device 44 °C, CPU 85");
        if (SetupPolicy.thermalStatusUnreliable(3, 400, 50f)) throw new AssertionError("40.0 °C");
        if (SetupPolicy.thermalStatusUnreliable(3, 310, 90f)) throw new AssertionError("CPU 90 °C");
        if (SetupPolicy.thermalStatusUnreliable(3, 310, 98.5f)) throw new AssertionError("CPU 98.5 °C");
        if (SetupPolicy.trustedThermal(3, 440, nan) != 3) throw new AssertionError("hot device kept");
        // no battery data: nothing contradicts SEVERE, it is believed
        if (SetupPolicy.thermalStatusUnreliable(3, -1, nan)) throw new AssertionError("no battery, -1");
        if (SetupPolicy.thermalStatusUnreliable(3, Integer.MIN_VALUE, 50f)) throw new AssertionError("no battery, MIN");
        // none / light / moderate are never doubted; CRITICAL and higher always believed; unknown stays unknown
        for (int status : new int[] {-1, 0, 1, 2, 4, 5, 6})
            if (SetupPolicy.thermalStatusUnreliable(status, 300, 60f) || SetupPolicy.trustedThermal(status, 300, 60f) != status)
                throw new AssertionError("status " + status);
        // setup: the EVO no longer pauses for heat, a hot device still does, CRITICAL still pauses
        SetupPolicy evo = new SetupPolicy();
        expect(null, evo.update(SetupPolicy.trustedThermal(3, 300, 60.1f), 300, 80, true, false));
        expect(null, evo.update(SetupPolicy.trustedThermal(3, 310, 77.2f), 310, 80, true, false));
        expect("heat", evo.update(SetupPolicy.trustedThermal(3, 310, 92f), 310, 80, true, false));
        expect("heat", new SetupPolicy().update(SetupPolicy.trustedThermal(3, 440, nan), 440, 80, true, false));
        expect("heat", new SetupPolicy().update(SetupPolicy.trustedThermal(3, -1, nan), -1, 80, true, false));
        expect(null, new SetupPolicy().update(SetupPolicy.trustedThermal(0, 300, nan), 300, 80, true, false));
        expect(null, new SetupPolicy().update(SetupPolicy.trustedThermal(2, 300, nan), 300, 80, true, false));
        expect("critical", new SetupPolicy().update(SetupPolicy.trustedThermal(4, 300, nan), 300, 80, true, false));
        // compile workers: the EVO keeps its parallel compilers, a hot device still gets one
        if (SetupPolicy.compileJobs(8, 10L << 30, false, SetupPolicy.trustedThermal(3, 300, 60.1f)) != 4)
            throw new AssertionError("EVO compile workers");
        if (SetupPolicy.compileJobs(8, 10L << 30, false, SetupPolicy.trustedThermal(3, 440, nan)) != 1)
            throw new AssertionError("hot device compile workers");
        // texts: performance report and setup readings
        expect("severe (reported by the system; battery 30.0 C, CPU/GPU 60.1 C, looks unreliable)",
            SetupPolicy.reportThermal(3, 300, 60.1f));
        expect("severe (reported by the system; battery 31.0 C, CPU/GPU n/a, looks unreliable)",
            SetupPolicy.reportThermal(3, 310, nan));
        expect("severe", SetupPolicy.reportThermal(3, 440, nan));
        expect("severe", SetupPolicy.reportThermal(3, Integer.MIN_VALUE, nan));
        expect("moderate", SetupPolicy.reportThermal(2, 300, nan));
        expect("none", SetupPolicy.reportThermal(0, 300, nan));
        expect("n/a", SetupPolicy.reportThermal(-1, Integer.MIN_VALUE, nan));
        expect("battery 30.0 °C, thermal status: severe (looks unreliable, ignored), CPU/GPU 77.2 °C",
            SetupPolicy.readings(3, 300, 77.2f));
        expect("battery 44.0 °C, thermal status: severe", SetupPolicy.readings(3, 440, nan));
        expect("Waiting for the Android device to cool down (battery 44.0 °C, thermal status: severe, CPU/GPU 61.5 °C)",
            SetupPolicy.description("heat", 3, 440, 61.5f));
        // CPU/GPU sensors from a sysfs copy: only cpu/gpu zones, millidegrees, junk skipped
        try {
            java.io.File root = java.nio.file.Files.createTempDirectory("thermal").toFile();
            String[][] zones = {{"cpu-1-1", "77200"}, {"gpuss-1", "58900"}, {"socd", "100"}, {"battery", "95000"},
                {"cpuss-3", "garbage"}, {"skin-therm", "99000"}};
            for (int z = 0; z < zones.length; ++z) {
                java.io.File zone = new java.io.File(root, "class/thermal/thermal_zone" + z);
                zone.mkdirs();
                java.nio.file.Files.writeString(new java.io.File(zone, "type").toPath(), zones[z][0] + "\n");
                java.nio.file.Files.writeString(new java.io.File(zone, "temp").toPath(), zones[z][1] + "\n");
            }
            if (SetupPolicy.hottestCoreSensor(root) != 77.2f) throw new AssertionError("hottest core " + SetupPolicy.hottestCoreSensor(root));
            if (!Float.isNaN(SetupPolicy.hottestCoreSensor(new java.io.File(root, "missing")))) throw new AssertionError("no sysfs");
        } catch (java.io.IOException e) { throw new AssertionError(e); }
        System.out.println("Setup policy checks passed");
    }
}
