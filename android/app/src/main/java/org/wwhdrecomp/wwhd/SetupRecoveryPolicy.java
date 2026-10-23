package org.wwhdrecomp.wwhd;

/** Automatic recovery never starts a merely selected dump or retries a failed job. */
public final class SetupRecoveryPolicy {
    private SetupRecoveryPolicy() {}

    public static boolean shouldStart(String state, boolean manualPause,
            boolean checkUpdate, boolean inputsChanged) {
        if (manualPause) return false;
        if ("running".equals(state) || "importing".equals(state) ||
                "preparing".equals(state) || "activating".equals(state) || "paused".equals(state) ||
                "preparing models".equals(state))  // setup's models step (ModelsStep.STATE)
            return true;
        return "complete".equals(state) && checkUpdate && inputsChanged;
    }
}
