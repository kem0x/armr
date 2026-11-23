package isomr;

public final class Probe {
    private static final Probe INSTANCE = new Probe();
    private int voidCalls;

    private Probe() {}

    public static Probe probe() {
        return INSTANCE;
    }

    public Probe instanceProbe() {
        return this;
    }

    public void voidProbe() {
        ++voidCalls;
    }

    public Probe voidProbeResult() {
        return voidCalls == 1 ? this : null;
    }

    public static void staticVoidProbe() {
    }
}
