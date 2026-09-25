#!/usr/bin/env python3
"""Write the Xcode and Visual Studio projects for cxx1 from the source tree.

The Makefile finds its sources with a wildcard, so a project file listing them
by hand rots the first time somebody adds one. This writes both from what is on
disk now:

    ./generate.py            regenerate both projects
    ./generate.py --check    say whether they are up to date, and change nothing

Every flag here is the one the Makefile or msvc/build.cmd already uses; where
the two toolchains differ, the difference is commented at the line that makes it.
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))            # the checkout
NAME = "cxx1"                # the project files keep their name...
PRODUCT = "cpp11"            # ...and build the program src/Name.h names
UP = ".."                    # from ide/ to the tree, in both project dialects


def sources():
    """Every .cpp the build compiles, in the Makefile's own order."""
    out = []
    for d in ("", "parser", "backend", "optimizer"):
        base = os.path.join(ROOT, "src", d)
        for f in sorted(os.listdir(base)):
            if f.endswith(".cpp") and " " not in f:      # macOS " 2.cpp" copies
                out.append(os.path.join("src", d, f).replace("\\", "/").replace("//", "/"))
    return out


def headers():
    out = []
    for d in ("", "parser", "backend", "optimizer"):
        base = os.path.join(ROOT, "src", d)
        for f in sorted(os.listdir(base)):
            if f.endswith(".h") and " " not in f:
                out.append(os.path.join("src", d, f).replace("\\", "/").replace("//", "/"))
    return out


def uid(text):
    """A stable 24-hex-digit id. Xcode only asks that they be unique and stable;
    deriving them from the path keeps a regenerated project diffable."""
    return hashlib.sha1(text.encode()).hexdigest()[:24].upper()


# ---------------------------------------------------------------- Xcode

def xcode(srcs, hdrs):
    """project.pbxproj for a command-line tool target.

    The flags are the Makefile's: -std=c++14 -O2 -g -Wall -Wextra -Werror
    -pedantic, and CXX1_INCLUDE_DIR pointing at the checkout's lib/, which the
    driver compiles into the binary as an absolute path.
    """
    files, builds, groups = [], [], {}
    for p in srcs + hdrs:
        fid, bid = uid("f:" + p), uid("b:" + p)
        kind = "sourcecode.cpp.cpp" if p.endswith(".cpp") else "sourcecode.c.h"
        files.append('\t\t%s /* %s */ = {isa = PBXFileReference; lastKnownFileType = %s; '
                     'name = %s; path = "%s"; sourceTree = "<group>"; };'
                     % (fid, os.path.basename(p), kind, os.path.basename(p),
                        UP + "/" + p))
        if p.endswith(".cpp"):
            builds.append('\t\t%s /* %s in Sources */ = {isa = PBXBuildFile; fileRef = %s /* %s */; };'
                          % (bid, os.path.basename(p), fid, os.path.basename(p)))
        groups.setdefault(os.path.dirname(p), []).append((fid, os.path.basename(p)))

    group_secs, group_children = [], []
    for d in sorted(groups):
        gid = uid("g:" + d)
        kids = "\n".join('\t\t\t\t%s /* %s */,' % (f, n) for f, n in sorted(groups[d], key=lambda x: x[1]))
        group_secs.append('\t\t%s /* %s */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n%s\n\t\t\t);\n'
                          '\t\t\tname = %s;\n\t\t\tsourceTree = "<group>";\n\t\t};'
                          % (gid, d or "src", kids, '"%s"' % (d or "src")))
        group_children.append('\t\t\t\t%s /* %s */,' % (gid, d or "src"))

    src_phase = "\n".join('\t\t\t\t%s /* %s in Sources */,' % (uid("b:" + p), os.path.basename(p))
                          for p in srcs)
    inc = os.path.join(ROOT, "lib")
    # **Both directories, or a compiler built here cannot find <vector>.**
    # `lib/` holds the C headers and `include/` the C++ ones on top of them;
    # the Makefile passes both and these projects passed only the first, so an
    # IDE-built binary compiled `<stddef.h>` and answered "cannot find <vector>"
    # - measured on 2026-09-09 with the Xcode binary, which is the only way this
    # would ever have been noticed. The driver also looks *beside itself* now,
    # which is what makes an unpacked release work; this is the other half, for
    # a binary sitting in DerivedData or in x64\Release with nothing beside it.
    cxxinc = os.path.join(ROOT, "include")
    common = ('\t\t\t\tALWAYS_SEARCH_USER_PATHS = NO;\n'
              # **The Makefile's warning line and no other.** Xcode's template
              # adds -Wshorten-64-to-32, which -Wall -Wextra do not, and it fires
              # on the bitfield arithmetic that is deliberately done in long long
              # and narrowed after a check. A project that builds this tree with
              # a different warning set is a fourth opinion nothing else gates on.
              '\t\t\t\tGCC_WARN_64_TO_32_BIT_CONVERSION = NO;\n'
              # One architecture, as `make` builds: the target is a run-time
              # choice here, so a universal binary buys nothing but time.
              '\t\t\t\tONLY_ACTIVE_ARCH = YES;\n'
              '\t\t\t\tCLANG_CXX_LANGUAGE_STANDARD = "c++14";\n'
              '\t\t\t\tCLANG_CXX_LIBRARY = "libc++";\n'
              '\t\t\t\tCLANG_ENABLE_OBJC_ARC = YES;\n'
              '\t\t\t\tCODE_SIGN_STYLE = Automatic;\n'
              # the Makefile's own warning set, and -Werror with it
              '\t\t\t\tGCC_TREAT_WARNINGS_AS_ERRORS = YES;\n'
              '\t\t\t\tWARNING_CFLAGS = (\n\t\t\t\t\t"-Wall",\n\t\t\t\t\t"-Wextra",\n'
              '\t\t\t\t\t"-pedantic",\n\t\t\t\t);\n'
              # the include directory the driver bakes in, as the Makefile does
              '\t\t\t\tGCC_PREPROCESSOR_DEFINITIONS = (\n'
              '\t\t\t\t\t"CXX1_INCLUDE_DIR=\\\\\\"%s\\\\\\"",\n'
              '\t\t\t\t\t"CXX1_CXX_INCLUDE_DIR=\\\\\\"%s\\\\\\"",\n\t\t\t\t);\n'
              '\t\t\t\tPRODUCT_NAME = "%s";\n'
              '\t\t\t\tHEADER_SEARCH_PATHS = "%s/src";\n' % (inc, cxxinc, PRODUCT, ROOT))
    return files, builds, group_secs, group_children, src_phase, common


def write_xcode(srcs, hdrs, check):
    files, builds, group_secs, group_children, src_phase, common = xcode(srcs, hdrs)
    proj = os.path.join(HERE, NAME + ".xcodeproj")
    pb = os.path.join(proj, "project.pbxproj")

    ids = {k: uid(k) for k in ("project", "target", "product", "productgroup",
                               "mainGroup", "sources", "cfgProject", "cfgTarget",
                               "dbgP", "relP", "dbgT", "relT")}
    text = f"""// !$*UTF8*$!
{{
	archiveVersion = 1;
	classes = {{}};
	objectVersion = 54;
	objects = {{

/* Begin PBXBuildFile section */
{chr(10).join(builds)}
/* End PBXBuildFile section */

/* Begin PBXFileReference section */
{chr(10).join(files)}
		{ids['product']} /* {NAME} */ = {{isa = PBXFileReference; explicitFileType = "compiled.mach-o.executable"; includeInIndex = 0; path = {NAME}; sourceTree = BUILT_PRODUCTS_DIR; }};
/* End PBXFileReference section */

/* Begin PBXGroup section */
		{ids['mainGroup']} = {{
			isa = PBXGroup;
			children = (
{chr(10).join(group_children)}
				{ids['productgroup']} /* Products */,
			);
			sourceTree = "<group>";
		}};
		{ids['productgroup']} /* Products */ = {{
			isa = PBXGroup;
			children = (
				{ids['product']} /* {NAME} */,
			);
			name = Products;
			sourceTree = "<group>";
		}};
{chr(10).join(group_secs)}
/* End PBXGroup section */

/* Begin PBXNativeTarget section */
		{ids['target']} /* {NAME} */ = {{
			isa = PBXNativeTarget;
			buildConfigurationList = {ids['cfgTarget']};
			buildPhases = (
				{ids['sources']} /* Sources */,
			);
			dependencies = ();
			name = {NAME};
			productName = {PRODUCT};
			productReference = {ids['product']} /* {NAME} */;
			productType = "com.apple.product-type.tool";
		}};
/* End PBXNativeTarget section */

/* Begin PBXProject section */
		{ids['project']} /* Project object */ = {{
			isa = PBXProject;
			attributes = {{
				BuildIndependentTargetsInParallel = 1;
				LastUpgradeCheck = 2600;
			}};
			buildConfigurationList = {ids['cfgProject']};
			compatibilityVersion = "Xcode 14.0";
			developmentRegion = en;
			hasScannedForEncodings = 0;
			knownRegions = (en, Base);
			mainGroup = {ids['mainGroup']};
			productRefGroup = {ids['productgroup']} /* Products */;
			projectDirPath = "";
			projectRoot = "";
			targets = (
				{ids['target']} /* {NAME} */,
			);
		}};
/* End PBXProject section */

/* Begin PBXSourcesBuildPhase section */
		{ids['sources']} /* Sources */ = {{
			isa = PBXSourcesBuildPhase;
			buildActionMask = 2147483647;
			files = (
{src_phase}
			);
			runOnlyForDeploymentPostprocessing = 0;
		}};
/* End PBXSourcesBuildPhase section */

/* Begin XCBuildConfiguration section */
		{ids['dbgP']} /* Debug */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
{common}				GCC_OPTIMIZATION_LEVEL = 0;
			}};
			name = Debug;
		}};
		{ids['relP']} /* Release */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
{common}				GCC_OPTIMIZATION_LEVEL = 2;
			}};
			name = Release;
		}};
		{ids['dbgT']} /* Debug */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
				PRODUCT_NAME = "{PRODUCT}";
			}};
			name = Debug;
		}};
		{ids['relT']} /* Release */ = {{
			isa = XCBuildConfiguration;
			buildSettings = {{
				PRODUCT_NAME = "{PRODUCT}";
			}};
			name = Release;
		}};
/* End XCBuildConfiguration section */

/* Begin XCConfigurationList section */
		{ids['cfgProject']} = {{
			isa = XCConfigurationList;
			buildConfigurations = (
				{ids['dbgP']} /* Debug */,
				{ids['relP']} /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		}};
		{ids['cfgTarget']} = {{
			isa = XCConfigurationList;
			buildConfigurations = (
				{ids['dbgT']} /* Debug */,
				{ids['relT']} /* Release */,
			);
			defaultConfigurationIsVisible = 0;
			defaultConfigurationName = Release;
		}};
/* End XCConfigurationList section */
	}};
	rootObject = {ids['project']} /* Project object */;
}}
"""
    ws = os.path.join(HERE, NAME + ".xcworkspace")
    wsdata = ('<?xml version="1.0" encoding="UTF-8"?>\n<Workspace version = "1.0">\n'
              '   <FileRef location = "group:%s.xcodeproj"></FileRef>\n</Workspace>\n' % NAME)

    # **A shared scheme, because an implicit one is not a file.** Xcode makes one
    # when a person opens the project and keeps it under xcuserdata, where it is
    # nobody else's; xcodebuild -scheme and any CI want one that is checked in.
    scheme = f"""<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion = "2600" version = "1.7">
   <BuildAction parallelizeBuildables = "YES" buildImplicitDependencies = "YES">
      <BuildActionEntries>
         <BuildActionEntry buildForTesting = "YES" buildForRunning = "YES" buildForProfiling = "YES" buildForArchiving = "YES" buildForAnalyzing = "YES">
            <BuildableReference
               BuildableIdentifier = "primary"
               BlueprintIdentifier = "{ids['target']}"
               BuildableName = "{NAME}"
               BlueprintName = "{NAME}"
               ReferencedContainer = "container:{NAME}.xcodeproj">
            </BuildableReference>
         </BuildActionEntry>
      </BuildActionEntries>
   </BuildAction>
   <LaunchAction buildConfiguration = "Release" selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB" launchStyle = "0" useCustomWorkingDirectory = "NO" ignoresPersistentStateOnLaunch = "NO" debugDocumentVersioning = "YES" debugServiceExtension = "internal" allowLocationSimulation = "YES">
      <BuildableProductRunnable runnableDebuggingMode = "0">
         <BuildableReference
            BuildableIdentifier = "primary"
            BlueprintIdentifier = "{ids['target']}"
            BuildableName = "{NAME}"
            BlueprintName = "{NAME}"
            ReferencedContainer = "container:{NAME}.xcodeproj">
         </BuildableReference>
      </BuildableProductRunnable>
   </LaunchAction>
   <AnalyzeAction buildConfiguration = "Release"></AnalyzeAction>
   <ArchiveAction buildConfiguration = "Release" revealArchiveInOrganizer = "YES"></ArchiveAction>
</Scheme>
"""
    schemedir = os.path.join(proj, "xcshareddata", "xcschemes")
    spath = os.path.join(schemedir, NAME + ".xcscheme")

    if check:
        old = open(pb).read() if os.path.exists(pb) else ""
        oldscheme = open(spath).read() if os.path.exists(spath) else ""
        return old == text and oldscheme == scheme
    os.makedirs(proj, exist_ok=True)
    os.makedirs(ws, exist_ok=True)
    os.makedirs(schemedir, exist_ok=True)
    open(pb, "w").write(text)
    open(spath, "w").write(scheme)
    open(os.path.join(ws, "contents.xcworkspacedata"), "w").write(wsdata)
    return True


# ------------------------------------------------------- Visual Studio 2022

def validXml(text, what):
    """**A project file is XML, and MSBuild will not read a control byte.**
    One backslash in a comment - `msvc\\build.cmd` written with one rather than
    two inside an f-string - put 0x08 in the file, and Visual Studio answered
    `MSB4025: hexadecimal value 0x08, is an invalid character` rather than
    anything about the compiler. Checked here so the generator cannot ship one
    again."""
    import xml.dom.minidom
    xml.dom.minidom.parseString(text.encode("utf-8"))
    for ch in text:
        if ord(ch) < 9 or ord(ch) in (11, 12) or 14 <= ord(ch) <= 31:
            raise ValueError("%s holds a control byte 0x%02X" % (what, ord(ch)))
    return text


def write_vs(srcs, hdrs, check):
    """cxx1.vcxproj and cxx1.sln, carrying msvc/build.cmd's flags exactly.

    /std:c++14 and /permissive- are the pin the other two toolchains spell
    -std=c++14; /W4 /WX with five warnings disabled is build.cmd's list, and
    every one of them fires on code this tree forked rather than wrote.
    """
    def win(p):
        return "..\\" + p.replace("/", "\\")

    cl = "\n".join('    <ClCompile Include="%s" />' % win(p) for p in srcs)
    hd = "\n".join('    <ClInclude Include="%s" />' % win(p) for p in hdrs)
    guid = "{" + uid("vs:" + NAME)[:8] + "-" + uid("vs:g1")[:4] + "-" + \
           uid("vs:g2")[:4] + "-" + uid("vs:g3")[:4] + "-" + uid("vs:g4")[:12] + "}"

    # The include directory is compiled into the binary as a C string literal,
    # so it must be spelled with forward slashes: a backslash there starts an
    # escape and the error lands in Driver.cpp, which is not the file at fault.
    incdir = "$([System.String]::Copy('$(ProjectDir)..\\lib').Replace('\\','/'))"
    cxxincdir = "$([System.String]::Copy('$(ProjectDir)..\\include').Replace('\\','/'))"

    proj = f"""<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{guid}</ProjectGuid>
    <RootNamespace>{NAME}</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration">
    <UseDebugLibraries>false</UseDebugLibraries>
    <WholeProgramOptimization>false</WholeProgramOptimization>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration">
    <UseDebugLibraries>true</UseDebugLibraries>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir>$(ProjectDir)build\\$(Configuration)\\</OutDir>
    <IntDir>$(ProjectDir)build\\$(Configuration)\\obj\\</IntDir>
    <TargetName>{PRODUCT}</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <LanguageStandard>stdcpp14</LanguageStandard>
      <ConformanceMode>true</ConformanceMode>
      <ExceptionHandling>Sync</ExceptionHandling>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <DisableSpecificWarnings>4996;4267;4244;4456;4146</DisableSpecificWarnings>
      <AdditionalIncludeDirectories>$(ProjectDir)..\\msvc\\compat;$(ProjectDir)..\\src;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>_CRT_SECURE_NO_WARNINGS;CXX1_INCLUDE_DIR="{incdir}";CXX1_CXX_INCLUDE_DIR="{cxxincdir}";%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
    </ClCompile>
    <Link><SubSystem>Console</SubSystem></Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
    </ClCompile>
    <Link><GenerateDebugInformation>true</GenerateDebugInformation></Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <ClCompile>
      <Optimization>Disabled</Optimization>
      <RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>
      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
      <!-- **/WX is off in Debug, and on in Release.** MSVC warns about things
           at /Od that /O2 hides - C4701 and C4703, "potentially uninitialized",
           are reported from a flow analysis that optimisation does differently
           - so a Debug build gated on -Werror fails on diagnostics the gate
           was never measured against. Release keeps the gate, which is the
           configuration msvc/build.cmd and tools/verify-three both use. -->
      <TreatWarningAsError>false</TreatWarningAsError>
    </ClCompile>
    <Link><GenerateDebugInformation>true</GenerateDebugInformation></Link>
  </ItemDefinitionGroup>
  <ItemGroup>
{cl}
  </ItemGroup>
  <ItemGroup>
{hd}
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
"""
    sln = f"""Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1
Project("{{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}}") = "{NAME}", "{NAME}.vcxproj", "{guid}"
EndProject
Global
\tGlobalSection(SolutionConfigurationPlatforms) = preSolution
\t\tDebug|x64 = Debug|x64
\t\tRelease|x64 = Release|x64
\tEndGlobalSection
\tGlobalSection(ProjectConfigurationPlatforms) = postSolution
\t\t{guid}.Debug|x64.ActiveCfg = Debug|x64
\t\t{guid}.Debug|x64.Build.0 = Debug|x64
\t\t{guid}.Release|x64.ActiveCfg = Release|x64
\t\t{guid}.Release|x64.Build.0 = Release|x64
\tEndGlobalSection
EndGlobal
"""
    # **The filters file is what makes it look like a project in the IDE.**
    # Without one Visual Studio shows 49 files in one flat list; with it the
    # tree is the tree - src, src\parser, src\backend, src\optimizer - as the
    # Xcode project has always presented it and how CLAUDE.md's "the parser is
    # twelve files" reads on disk.
    def folder(p):
        parts = p.split("/")
        return "\\".join(parts[:-1]).replace("src", "Source Files", 1)

    def hfolder(p):
        parts = p.split("/")
        return "\\".join(parts[:-1]).replace("src", "Header Files", 1)

    dirs = sorted({folder(p) for p in srcs} | {hfolder(p) for p in hdrs})
    unique = []
    for d in dirs:                      # every parent folder, once each
        parts = d.split("\\")
        for i in range(1, len(parts) + 1):
            joined = "\\".join(parts[:i])
            if joined and joined not in unique:
                unique.append(joined)
    filt_dirs = "\n".join(
        '    <Filter Include="%s"><UniqueIdentifier>{%s}</UniqueIdentifier></Filter>'
        % (d, uid("filt:" + d)[:8] + "-" + uid("f1:" + d)[:4] + "-" +
           uid("f2:" + d)[:4] + "-" + uid("f3:" + d)[:4] + "-" +
           uid("f4:" + d)[:12]) for d in unique)
    filt_src = "\n".join(
        '    <ClCompile Include="%s"><Filter>%s</Filter></ClCompile>'
        % (win(p), folder(p)) for p in srcs)
    filt_hdr = "\n".join(
        '    <ClInclude Include="%s"><Filter>%s</Filter></ClInclude>'
        % (win(p), hfolder(p)) for p in hdrs)
    filters = f"""<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
{filt_dirs}
  </ItemGroup>
  <ItemGroup>
{filt_src}
  </ItemGroup>
  <ItemGroup>
{filt_hdr}
  </ItemGroup>
</Project>
"""

    # **And a solution at the root of the tree**, which is where somebody who
    # has just unpacked this looks first. It is the same project, named by a
    # path: two files, one project, no copy of anything.
    rootSln = sln.replace('"%s.vcxproj"' % NAME, '"ide\\%s.vcxproj"' % NAME)

    pp = os.path.join(HERE, NAME + ".vcxproj")
    sp = os.path.join(HERE, NAME + ".sln")
    fp = pp + ".filters"
    rp = os.path.join(ROOT, NAME + ".sln")
    if check:
        for path, want in ((pp, proj), (sp, sln), (fp, filters), (rp, rootSln)):
            if not os.path.exists(path) or open(path).read() != want:
                return False
        return True
    validXml(proj, "cxx1.vcxproj")
    validXml(filters, "cxx1.vcxproj.filters")
    for path, want in ((pp, proj), (sp, sln), (fp, filters), (rp, rootSln)):
        open(path, "w", newline="\r\n").write(want)
    return True


if __name__ == "__main__":
    check = "--check" in sys.argv
    s, h = sources(), headers()
    x, v = write_xcode(s, h, check), write_vs(s, h, check)
    if check:
        print("up to date" if (x and v) else "STALE - run ./generate.py")
        sys.exit(0 if (x and v) else 1)
    print("wrote %s.xcodeproj, %s.xcworkspace, %s.vcxproj and %s.sln for %d sources"
          % (NAME, NAME, NAME, NAME, len(s)))
