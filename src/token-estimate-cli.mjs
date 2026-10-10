#!/usr/bin/env node
/**
 * skill-family-token-estimate — CLI entry of the authoritative deterministic
 * token estimator (audit remediation C1).
 *
 * Usage:
 *   skill-family-token-estimate (--text <string> | --file <path> | --stdin)
 *                               [--estimator tokens|upper-bound|both]
 *
 * Input selection (exactly one, refused otherwise):
 *   --text <string>   estimate the literal string argument;
 *   --file <path>     estimate the UTF-8 content of the file at <path>;
 *   --stdin           estimate the UTF-8 content read from standard input.
 *
 * Estimator selection:
 *   --estimator tokens        the authoritative token-count record
 *                             (estimateTokens, algorithm
 *                             cjk-char-whitespace-split) — the default;
 *   --estimator upper-bound   the UTF-8 byte upper-bound record
 *                             (estimateTokenUpperBound, FND-ADR-009);
 *   --estimator both          one JSON object { tokens, upperBound } with
 *                             both records.
 *
 * The selected record documents are printed to stdout as JSON. Every record
 * is deterministic: identical input bytes always produce identical output
 * bytes, and the token record carries the estimator identity and version
 * (SFA-CONTEXT-028). Exit codes: 0 success; 2 usage or input errors.
 * The CLI performs no writes other than stdout/stderr.
 */
import { readFileSync, readSync } from "node:fs";
import { realpathSync } from "node:fs";
import process from "node:process";
import { fileURLToPath } from "node:url";
import {
  estimateTokens,
  estimateTokenUpperBound,
  TOKEN_ESTIMATION_ALGORITHM,
  TOKEN_ESTIMATOR_ID,
  TOKEN_ESTIMATOR_VERSION,
} from "./token-estimate.mjs";

const USAGE = `skill-family-token-estimate —— 权威确定性词元估算器 CLI

用法: skill-family-token-estimate (--text <string> | --file <path> | --stdin) [--estimator tokens|upper-bound|both]

输入（三者恰好选一，否则退出 2）:
  --text <string>    估算字面字符串
  --file <path>      估算 UTF-8 文件内容
  --stdin            估算标准输入的 UTF-8 内容

估算器:
  --estimator tokens        权威词元计数记录（默认；算法 ${TOKEN_ESTIMATION_ALGORITHM}，
                            估算器 ${TOKEN_ESTIMATOR_ID}@${TOKEN_ESTIMATOR_VERSION}）
  --estimator upper-bound   UTF-8 字节上界记录（FND-ADR-009）
  --estimator both          同时输出两个记录: { tokens, upperBound }

输出: 记录 JSON 写入 stdout；确定性——相同输入字节必然产生相同输出字节。
退出码: 0 成功；2 用法或输入错误。除 stdout/stderr 外零写入。
`;

const ESTIMATOR_MODES = ["tokens", "upper-bound", "both"];

export function parseTokenEstimateArgs(argv) {
  const options = { source: null, text: undefined, file: undefined, estimator: "tokens" };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--help" || arg === "-h") {
      options.help = true;
    } else if (arg === "--text" || arg === "--file" || arg === "--estimator") {
      const value = argv[i + 1];
      if (value === undefined) {
        throw new Error(`option ${arg} requires a value`);
      }
      i += 1;
      if (arg === "--text") {
        if (options.source !== null) throw new Error("exactly one input source is allowed (--text | --file | --stdin)");
        options.source = "text";
        options.text = value;
      } else if (arg === "--file") {
        if (options.source !== null) throw new Error("exactly one input source is allowed (--text | --file | --stdin)");
        options.source = "file";
        options.file = value;
      } else {
        if (!ESTIMATOR_MODES.includes(value)) {
          throw new Error(`--estimator must be one of: ${ESTIMATOR_MODES.join(", ")}`);
        }
        options.estimator = value;
      }
    } else if (arg === "--stdin") {
      if (options.source !== null) throw new Error("exactly one input source is allowed (--text | --file | --stdin)");
      options.source = "stdin";
    } else {
      throw new Error(`unknown option: ${arg}`);
    }
  }
  if (!options.help && options.source === null) {
    throw new Error("an input source is required: --text <string> | --file <path> | --stdin");
  }
  return options;
}

function readStdin() {
  const chunks = [];
  const buffer = Buffer.alloc(65536);
  // Synchronous stdin drain: the CLI is a bounded batch tool, never a daemon.
  for (;;) {
    let bytesRead = 0;
    try {
      bytesRead = readSync(0, buffer, 0, buffer.length, null);
    } catch (cause) {
      if (cause && cause.code === "EOF") break;
      throw cause;
    }
    if (bytesRead === 0) break;
    chunks.push(Buffer.from(buffer.subarray(0, bytesRead)));
  }
  return Buffer.concat(chunks).toString("utf8");
}

export function buildEstimateOutput(text, estimator) {
  if (estimator === "tokens") return estimateTokens(text);
  if (estimator === "upper-bound") return estimateTokenUpperBound(text);
  return { tokens: estimateTokens(text), upperBound: estimateTokenUpperBound(text) };
}

export async function tokenEstimateCliMain(argv) {
  let options;
  try {
    options = parseTokenEstimateArgs(argv);
  } catch (cause) {
    process.stderr.write(`[token-estimate] ${cause.message}\n`);
    return 2;
  }
  if (options.help) {
    process.stdout.write(USAGE);
    return 0;
  }
  let text;
  if (options.source === "text") {
    text = options.text;
  } else if (options.source === "file") {
    try {
      text = readFileSync(options.file, "utf8");
    } catch (cause) {
      process.stderr.write(`[token-estimate] cannot read --file input: ${cause && cause.code ? cause.code : "unknown"}\n`);
      return 2;
    }
  } else {
    try {
      text = readStdin();
    } catch (cause) {
      process.stderr.write(`[token-estimate] cannot read --stdin input: ${cause && cause.code ? cause.code : "unknown"}\n`);
      return 2;
    }
  }
  const output = buildEstimateOutput(text, options.estimator);
  process.stdout.write(`${JSON.stringify(output, null, 2)}\n`);
  return 0;
}

// Direct-execution detection must survive symlinked package bins (pnpm
// .bin shims): compare real paths, not the raw argv[1] URL.
function isDirectExecution() {
  if (process.argv[1] === undefined) return false;
  try {
    return realpathSync(process.argv[1]) === realpathSync(fileURLToPath(import.meta.url));
  } catch {
    return false;
  }
}
if (isDirectExecution()) {
  tokenEstimateCliMain(process.argv.slice(2)).then((code) => {
    process.exit(code);
  });
}
