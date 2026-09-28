// dss runca.js <ccxml> <program.out> <cio.txt> [clock event index, 0 = cycle.Total] [timeout ms] [cache]
// "cache" sets MAR192-207, making the 256 MB SDRAM at 0xC0000000 cacheable as a program's startup
// code would: after reset the C674x caches no external memory, and every access goes to SDRAM.
// Any later argument NAME=VALUE writes VALUE into the global word NAME before the run.
// One program on the C6747 simulator: fresh connection, one clock event. Counters do not
// reset between runs of one session, so every run is a session of its own.
// Prints one line: RESULT event=<name> count=<n> pc=<hex> exit=<hex> rc=n/a.
// rc is n/a because TI's startup ignores what main returns and calls exit(1), and
// breakpoints set from DSS did not stop this simulator (2026-09-28), so it is not read.
importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
var env = ScriptingEnvironment.instance();
env.traceSetConsoleLevel(TraceLevel.WARNING);
env.setScriptTimeout(arguments.length > 4 ? parseInt(arguments[4]) : 600000);
var server = env.getServer("DebugServer.1");
server.setConfig(arguments[0]);
var session = server.openSession(".*");
session.target.connect();
var ev = arguments.length > 3 ? parseInt(arguments[3]) : 0;
session.clock.setCurrentEvent(ev);
session.clock.enable();
session.beginCIOLogging(arguments[2]);
session.memory.loadProgram(arguments[1]);
if (arguments.length > 5 && arguments[5] == "cache") {
    for (var m = 192; m < 208; m++) session.memory.writeWord(0, 0x01848000 + 4 * m, 1);
}
for (var k = 6; k < arguments.length; k++) {
    var eq = arguments[k].indexOf("=");
    if (eq > 0) session.memory.writeWord(0, session.symbol.getAddress(arguments[k].substring(0, eq)),
                                         parseInt(arguments[k].substring(eq + 1)));
}
session.clock.reset();
session.target.run();
var count = session.clock.read();
session.endCIOLogging();
var pc = session.expression.evaluate("PC");
var exitAt = -1;
try { exitAt = session.symbol.getAddress("C$$EXIT"); } catch (e) { }
print("RESULT event=" + session.clock.getEventName(ev) + " count=" + count
      + " pc=0x" + Long.toHexString(pc) + " exit=0x" + Long.toHexString(exitAt) + " rc=n/a");
session.target.disconnect();
session.terminate();
server.stop();
