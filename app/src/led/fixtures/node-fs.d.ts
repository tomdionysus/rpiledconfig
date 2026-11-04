// The two Node calls the fixture loader makes, declared here so the app's
// typecheck does not take on all of Node's globals (which disagree with React
// Native's about timers) for one test helper.
declare module 'node:fs' {
  export function readdirSync(path: string): string[];
  export function readFileSync(path: string, encoding: 'utf8'): string;
}
