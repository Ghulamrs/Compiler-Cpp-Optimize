// dss runca.js <ccxml> <program.out> <cio.txt> [clock event index, 0 = cycle.Total]
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
env.setScriptTimeout(600000);
var server = env.getServer("DebugServer.1");
server.setConfig(arguments[0]);
var session = server.openSession(".*");
session.target.connect();
var ev = arguments.length > 3 ? parseInt(arguments[3]) : 0;
session.clock.setCurrentEvent(ev);
session.clock.enable();
session.beginCIOLogging(arguments[2]);
session.memory.loadProgram(arguments[1]);
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
