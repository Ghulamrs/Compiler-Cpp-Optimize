// dss prof.js <ccxml> <program.out> <cio.txt> <interval ms> [NAME=VALUE ...]
// A sampling profiler for the C6747 simulator: run, halt every <interval> of wall time, print
// the program counter, resume - until the program stops at C$$EXIT. NAME=VALUE writes a global
// word before the run, as runca.js does. Map the SAMPLE lines to functions with the image's
// symbol table (tools/c6747/prof-map.py).
importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
var env = ScriptingEnvironment.instance();
env.traceSetConsoleLevel(TraceLevel.WARNING);
env.setScriptTimeout(86400000);
var server = env.getServer("DebugServer.1");
server.setConfig(arguments[0]);
var session = server.openSession(".*");
session.target.connect();
session.clock.setCurrentEvent(0);
session.clock.enable();
session.beginCIOLogging(arguments[2]);
session.memory.loadProgram(arguments[1]);
for (var k = 4; k < arguments.length; k++) {
    var eq = arguments[k].indexOf("=");
    if (eq > 0) session.memory.writeWord(0, session.symbol.getAddress(arguments[k].substring(0, eq)),
                                         parseInt(arguments[k].substring(eq + 1)));
}
session.clock.reset();
var exitAt = Number(session.symbol.getAddress("C$$EXIT"));
var interval = parseInt(arguments[3]);
var n = 0;
var same = 0, lastPc = -1, stuck = false;
session.target.runAsynch();
while (true) {
    Thread.sleep(interval);
    if (!session.target.isHalted()) session.target.halt();
    var pc = Number(session.expression.evaluate("PC"));
    if (pc == exitAt) break;
    // A crashed program spins: thirty samples at one address is not a profile, it is a hang.
    same = (pc == lastPc) ? same + 1 : 0;
    lastPc = pc;
    if (same >= 30) { stuck = true; break; }
    print("SAMPLE 0x" + pc.toString(16) + " " + session.clock.read());
    n++;
    session.target.runAsynch();
}
session.endCIOLogging();
print("RESULT samples=" + n + " cycles=" + session.clock.read() + (stuck ? " stuck=0x" + lastPc.toString(16) : ""));
session.target.disconnect();
session.terminate();
server.stop();
