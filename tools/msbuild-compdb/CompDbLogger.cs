// MSBuild logger that writes compile_commands.json for lcart from the CL tasks
// of a build (NFR-013 criterion 3, ADR-0022).
//
//   MSBuild.exe Solution.sln -t:Rebuild -nologo -v:q
//       "-logger:LcartMsbuildCompdb.CompDbLogger,CompDbLogger.dll;out=compile_commands.json"
//
// Parameters, separated by '|' (a Windows path may contain ';' or spaces):
//   out=<file>        output file (default compile_commands.json)
//   map=<from>=<to>   replace the path prefix <from> with <to> in every path of
//                     the database, and turn '\' into '/' in them; repeatable.
//                     Example: map=C:\src=/mnt/c/src
//
// No third-party code: only Microsoft.Build.Framework from the MSBuild that runs
// the build. Source: the TaskCommandLineEventArgs of the CL task (the command
// line MSBuild really runs) and the evaluated IncludePath / ExternalIncludePath
// of the project (what MSBuild passes to CL.exe in %INCLUDE% and
// %EXTERNAL_INCLUDE%, not in the command line).

using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Microsoft.Build.Framework;

namespace LcartMsbuildCompdb
{
    public sealed class CompDbLogger : ILogger
    {
        sealed class Entry
        {
            public string Directory;
            public string File;
            public List<string> Arguments;
        }

        // CL options whose value is a path (after the option text, joined).
        static readonly string[] PathOptions =
        {
            "/external:I", "/imsvc", "/AI", "/FI", "/FU", "/Fo", "/Fd", "/Fe", "/Fa", "/FR", "/Fr", "/Fm", "/I",
        };

        // Precompiled-header options: dropped, because the translation unit
        // includes the header itself and clang-cl would look for a .pch.
        static readonly string[] PchOptions = { "/Yc", "/Yu", "/Yl", "/Fp" };

        // Options that MSBuild writes with the value as the next argument.
        static readonly string[] SeparateValueOptions = { "/D", "/U" };

        public LoggerVerbosity Verbosity { get; set; }
        public string Parameters { get; set; }

        readonly Dictionary<int, Dictionary<string, string>> evaluations = new Dictionary<int, Dictionary<string, string>>();
        readonly Dictionary<int, int> evaluationOfProject = new Dictionary<int, int>();
        readonly List<Entry> entries = new List<Entry>();
        readonly List<KeyValuePair<string, string>> maps = new List<KeyValuePair<string, string>>();
        string outFile = "compile_commands.json";
        int clTasks;

        public void Initialize(IEventSource eventSource)
        {
            ParseParameters();
            IEventSource4 source4 = eventSource as IEventSource4;
            if (source4 != null)
                source4.IncludeEvaluationPropertiesAndItems();
            eventSource.AnyEventRaised += OnEvent;
        }

        void ParseParameters()
        {
            if (string.IsNullOrEmpty(Parameters))
                return;
            foreach (string part in Parameters.Split('|'))
            {
                if (part.StartsWith("out=", StringComparison.Ordinal))
                    outFile = part.Substring(4);
                else if (part.StartsWith("map=", StringComparison.Ordinal))
                {
                    string spec = part.Substring(4);
                    int eq = spec.LastIndexOf('=');
                    if (eq <= 0)
                        throw new LoggerException("map=<from>=<to> expected, got: " + part);
                    maps.Add(new KeyValuePair<string, string>(spec.Substring(0, eq), spec.Substring(eq + 1)));
                }
                else if (part.Length != 0)
                    throw new LoggerException("unknown parameter: " + part);
            }
        }

        void OnEvent(object sender, BuildEventArgs e)
        {
            ProjectEvaluationFinishedEventArgs evaluation = e as ProjectEvaluationFinishedEventArgs;
            if (evaluation != null)
            {
                evaluations[evaluation.BuildEventContext.EvaluationId] = ToDictionary(evaluation.Properties);
                return;
            }
            ProjectStartedEventArgs started = e as ProjectStartedEventArgs;
            if (started != null)
            {
                evaluationOfProject[started.BuildEventContext.ProjectContextId] = started.BuildEventContext.EvaluationId;
                return;
            }
            TaskCommandLineEventArgs task = e as TaskCommandLineEventArgs;
            if (task != null && string.Equals(task.TaskName, "CL", StringComparison.OrdinalIgnoreCase))
            {
                clTasks++;
                Dictionary<string, string> properties = null;
                int evaluationId;
                if (evaluationOfProject.TryGetValue(task.BuildEventContext.ProjectContextId, out evaluationId))
                    evaluations.TryGetValue(evaluationId, out properties);
                AddCommand(task.CommandLine, Path.GetDirectoryName(task.ProjectFile), properties);
            }
        }

        static Dictionary<string, string> ToDictionary(IEnumerable properties)
        {
            Dictionary<string, string> result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (properties == null)
                return result;
            foreach (object item in properties)
            {
                // MSBuild 17 hands over Microsoft.Build.Execution.ProjectPropertyInstance
                // objects (Name, EvaluatedValue); other versions and replayed
                // binary logs use DictionaryEntry or KeyValuePair<string, string>.
                // Reflection keeps this file independent of Microsoft.Build.dll.
                if (item is DictionaryEntry)
                {
                    DictionaryEntry pair = (DictionaryEntry)item;
                    result[(string)pair.Key] = pair.Value as string;
                    continue;
                }
                Type type = item.GetType();
                System.Reflection.PropertyInfo name = type.GetProperty("Name") ?? type.GetProperty("Key");
                System.Reflection.PropertyInfo value = type.GetProperty("EvaluatedValue") ?? type.GetProperty("Value");
                if (name != null && value != null)
                    result[name.GetValue(item, null) as string ?? ""] = value.GetValue(item, null) as string;
            }
            return result;
        }

        void AddCommand(string commandLine, string projectDirectory, Dictionary<string, string> properties)
        {
            // MSBuild logs "<path of CL.exe> <arguments>", the path unquoted
            // even when it contains spaces: cut at the first ".exe ".
            int exe = commandLine.IndexOf(".exe ", StringComparison.OrdinalIgnoreCase);
            if (exe < 0)
                return;
            string program = commandLine.Substring(0, exe + 4);
            List<string> tokens = SplitWindowsCommandLine(commandLine.Substring(exe + 5));

            // MSBuild writes the options first, then the source files.
            List<string> options = new List<string>();
            List<string> sources = new List<string>();
            for (int i = 0; i < tokens.Count; i++)
            {
                string token = tokens[i];
                if (token.StartsWith("/", StringComparison.Ordinal) || token.StartsWith("-", StringComparison.Ordinal))
                {
                    if (StartsWithAny(token, PchOptions))
                        continue;
                    options.Add(token);
                    if (Array.IndexOf(SeparateValueOptions, token) >= 0 && i + 1 < tokens.Count)
                        options.Add(tokens[++i]);
                }
                else
                    sources.Add(token);
            }

            List<string> prefix = new List<string>();
            prefix.Add("cl.exe"); // lower case: see ADR-0021, "CL.EXE" is not recognised on Linux
            string target = TargetOf(program);
            if (target != null)
                prefix.Add("--target=" + target);

            List<string> converted = new List<string>();
            foreach (string option in options)
                converted.Add(ConvertOption(option));

            // System include directories: MSBuild gives them to CL.exe in the
            // environment, so they are not in the command line. Name them.
            List<string> systemIncludes = new List<string>();
            if (properties != null)
            {
                foreach (string name in new[] { "ExternalIncludePath", "IncludePath" })
                {
                    string value;
                    if (!properties.TryGetValue(name, out value) || value == null)
                        continue;
                    foreach (string dir in value.Split(';'))
                    {
                        string trimmed = dir.Trim();
                        if (trimmed.Length == 0 || !Path.IsPathRooted(trimmed) || !System.IO.Directory.Exists(trimmed))
                            continue;
                        string mapped = MapPath(trimmed);
                        if (!systemIncludes.Contains(mapped))
                            systemIncludes.Add(mapped);
                    }
                }
            }

            foreach (string source in sources)
            {
                string absolute = Path.GetFullPath(Path.Combine(projectDirectory, source));
                Entry entry = new Entry();
                entry.Directory = MapPath(projectDirectory);
                entry.File = MapPath(absolute);
                entry.Arguments = new List<string>(prefix);
                entry.Arguments.AddRange(converted);
                foreach (string dir in systemIncludes)
                {
                    entry.Arguments.Add("/imsvc");
                    entry.Arguments.Add(dir);
                }
                entry.Arguments.Add(entry.File);
                entries.Add(entry);
            }
        }

        // The target architecture is the last directory of the CL.exe path
        // (...\bin\HostX64\x86\CL.exe). clang-cl defaults to x86_64.
        static string TargetOf(string program)
        {
            string dir = Path.GetFileName(Path.GetDirectoryName(program));
            if (string.Equals(dir, "x86", StringComparison.OrdinalIgnoreCase))
                return "i686-pc-windows-msvc";
            if (string.Equals(dir, "arm64", StringComparison.OrdinalIgnoreCase))
                return "aarch64-pc-windows-msvc";
            if (string.Equals(dir, "arm", StringComparison.OrdinalIgnoreCase))
                return "thumbv7-pc-windows-msvc";
            return null;
        }

        static bool StartsWithAny(string token, string[] options)
        {
            foreach (string option in options)
                if (token.StartsWith(option, StringComparison.Ordinal))
                    return true;
            return false;
        }

        string ConvertOption(string option)
        {
            foreach (string name in PathOptions)
            {
                if (!option.StartsWith(name, StringComparison.Ordinal))
                    continue;
                // No option in the table is a prefix of another one in it
                // ("/imsvc" starts with a lower-case "i", "/I" with a capital).
                string value = option.Substring(name.Length);
                if (value.Length == 0)
                    return option;
                return name + MapPath(value);
            }
            return option;
        }

        // Replaces a mapped prefix, then turns backslashes into slashes.
        string MapPath(string path)
        {
            string normalized = path;
            foreach (KeyValuePair<string, string> map in maps)
            {
                string from = map.Key.TrimEnd('\\', '/');
                string to = map.Value.TrimEnd('/');
                if (normalized.Length >= from.Length
                    && string.Compare(normalized, 0, from, 0, from.Length, StringComparison.OrdinalIgnoreCase) == 0
                    && (normalized.Length == from.Length || normalized[from.Length] == '\\' || normalized[from.Length] == '/'))
                {
                    normalized = to + normalized.Substring(from.Length);
                    break;
                }
            }
            return maps.Count == 0 ? path : normalized.Replace('\\', '/');
        }

        // CommandLineToArgvW rules.
        static List<string> SplitWindowsCommandLine(string text)
        {
            List<string> result = new List<string>();
            StringBuilder current = new StringBuilder();
            bool inToken = false;
            bool inQuotes = false;
            for (int i = 0; i < text.Length; i++)
            {
                char c = text[i];
                if (c == '\\')
                {
                    int slashes = 0;
                    while (i < text.Length && text[i] == '\\')
                    {
                        slashes++;
                        i++;
                    }
                    inToken = true;
                    if (i < text.Length && text[i] == '"')
                    {
                        current.Append('\\', slashes / 2);
                        if (slashes % 2 == 1)
                            current.Append('"');
                        else
                            inQuotes = !inQuotes;
                    }
                    else
                    {
                        current.Append('\\', slashes);
                        i--;
                    }
                }
                else if (c == '"')
                {
                    inQuotes = !inQuotes;
                    inToken = true;
                }
                else if ((c == ' ' || c == '\t' || c == '\r' || c == '\n') && !inQuotes)
                {
                    if (inToken)
                    {
                        result.Add(current.ToString());
                        current.Length = 0;
                        inToken = false;
                    }
                }
                else
                {
                    current.Append(c);
                    inToken = true;
                }
            }
            if (inToken)
                result.Add(current.ToString());
            return result;
        }

        public void Shutdown()
        {
            // One entry per (directory, file, arguments); sorted, so that the
            // result does not depend on /m scheduling (NFR-004).
            Dictionary<string, Entry> unique = new Dictionary<string, Entry>(StringComparer.Ordinal);
            foreach (Entry entry in entries)
                unique[entry.Directory + "\n" + entry.File + "\n" + string.Join("\n", entry.Arguments.ToArray())] = entry;
            List<string> keys = new List<string>(unique.Keys);
            keys.Sort(StringComparer.Ordinal);

            StringBuilder json = new StringBuilder();
            json.Append("[\n");
            for (int i = 0; i < keys.Count; i++)
            {
                Entry entry = unique[keys[i]];
                json.Append("  {\"directory\": ").Append(Quote(entry.Directory));
                json.Append(", \"file\": ").Append(Quote(entry.File));
                json.Append(", \"arguments\": [");
                for (int a = 0; a < entry.Arguments.Count; a++)
                {
                    if (a != 0)
                        json.Append(", ");
                    json.Append(Quote(entry.Arguments[a]));
                }
                json.Append("]}");
                json.Append(i + 1 < keys.Count ? ",\n" : "\n");
            }
            json.Append("]\n");
            File.WriteAllText(outFile, json.ToString(), new UTF8Encoding(false));
            Console.Out.WriteLine("compdb: {0} CL task(s), {1} entr{2} written to {3}", clTasks, keys.Count,
                                  keys.Count == 1 ? "y" : "ies", Path.GetFullPath(outFile));
            if (keys.Count == 0)
                Console.Out.WriteLine("compdb: no CL task ran. Build with -t:Rebuild: an up-to-date project runs no compiler.");
        }

        static string Quote(string value)
        {
            StringBuilder b = new StringBuilder("\"");
            foreach (char c in value)
            {
                switch (c)
                {
                    case '"': b.Append("\\\""); break;
                    case '\\': b.Append("\\\\"); break;
                    case '\n': b.Append("\\n"); break;
                    case '\r': b.Append("\\r"); break;
                    case '\t': b.Append("\\t"); break;
                    default:
                        if (c < 0x20 || c > 0x7e)
                            b.AppendFormat("\\u{0:x4}", (int)c);
                        else
                            b.Append(c);
                        break;
                }
            }
            return b.Append('"').ToString();
        }
    }
}
