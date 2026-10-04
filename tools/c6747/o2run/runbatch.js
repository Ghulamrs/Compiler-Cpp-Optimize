// dss runbatch.js <ccxml> <list file>
// Each line of the list: <program.out> <cio file> <result file> <timeout ms> [NAME=VALUE ...] [-- arg ...]
// One debug server, one fresh session per program (the clock does not reset between runs
// of one session). Writes a RESULT line into <result file>; a program that does not stop
// within the timeout is halted and recorded as timeout. cycle.Total (event 0).
importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
importPackage(Packages.java.io);
var env = ScriptingEnvironment.instance();
env.traceSetConsoleLevel(TraceLevel.OFF);
var server = env.getServer("DebugServer.1");
server.setConfig(arguments[0]);
var rd = new BufferedReader(new FileReader(arguments[1]));
var line;
function write(f, s) { var w = new FileWriter(f); w.write(s + "\n"); w.close(); }
while ((line = rd.readLine()) != null) {
    var t = String(line).replace(/^\s+|\s+$/g, "").split(/\s+/);
    if (t.length < 4) continue;
    var tmo = parseInt(t[3]);
    if (new File(t[2]).exists()) continue;           // resumed: already measured
    var args = [], sets = [];
    for (var a = 4; a < t.length; a++) {
        if (t[a] == "--") { for (var b = a + 1; b < t.length; b++) args.push(t[b]); break; }
        if (t[a].indexOf("=") > 0) sets.push(t[a]);
    }
    // A session that fails to initialise the simulated CPU (two lanes initialising at once) is tried
    // again on this lane at once, after a random 1-3 s, up to five times, before a failure is written.
    var session = null, res, attempt;
    var t0 = System.currentTimeMillis();
    for (attempt = 1; attempt <= 5; attempt++) {
        session = null;
        try {
            env.setScriptTimeout(120000);
            session = server.openSession(".*");
            session.target.connect();
            break;
        } catch (e) {
            res = "RESULT failed attempts=" + attempt + " " + String(e).replace(/\s+/g, "_").substring(0, 200);
            try { session.terminate(); } catch (e2) { }
            session = null;
            Thread.sleep(1000 + Math.floor(Math.random() * 2000));
        }
    }
    if (session != null) {
        try {
            session.clock.setCurrentEvent(0);
            session.clock.enable();
            session.beginCIOLogging(t[1]);
            if (args.length > 0) session.memory.loadProgram(t[0], args); else session.memory.loadProgram(t[0]);
            for (var k = 0; k < sets.length; k++) {
                var eq = sets[k].indexOf("=");
                session.memory.writeWord(0, session.symbol.getAddress(sets[k].substring(0, eq)), parseInt(sets[k].substring(eq + 1)));
            }
            session.clock.reset();
            env.setScriptTimeout(tmo);
            var timedOut = false;
            try { session.target.run(); } catch (e) { timedOut = true; }
            env.setScriptTimeout(120000);
            if (timedOut) { try { session.target.halt(); } catch (e) { } }
            var count = session.clock.read();
            try { session.endCIOLogging(); } catch (e) { }
            var pc = session.expression.evaluate("PC");
            var exitAt = -1;
            try { exitAt = session.symbol.getAddress("C$$EXIT"); } catch (e) { }
            res = "RESULT event=cycle.Total count=" + count + " pc=0x" + Long.toHexString(pc)
                + " exit=0x" + Long.toHexString(exitAt) + (timedOut ? " timeout=1" : "")
                + (attempt > 1 ? " attempts=" + attempt : "") + " wall_ms=" + (System.currentTimeMillis() - t0);
        } catch (e) {
            res = "RESULT failed " + String(e).replace(/\s+/g, "_").substring(0, 200);
        }
    }
    try { session.target.disconnect(); } catch (e) { }
    try { session.terminate(); } catch (e) { }
    write(t[2], res);
    print(t[0] + " " + res);
}
server.stop();
