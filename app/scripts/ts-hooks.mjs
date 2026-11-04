// Module resolution for running src/ TypeScript under Node's type stripping
// from a script: the `@/` alias the app's tsconfig defines, and the
// extensionless relative imports the app's sources use.
const SRC = new URL('../src/', import.meta.url);
const HAS_EXTENSION = /\.[cm]?[jt]sx?$|\.json$/;

export async function resolve(specifier, context, nextResolve) {
  const mapped = specifier.startsWith('@/') ? new URL(specifier.slice(2), SRC).href : specifier;
  const local = mapped.startsWith('.') || mapped.startsWith('file:');
  if (local && !HAS_EXTENSION.test(mapped)) {
    try {
      return await nextResolve(`${mapped}.ts`, context);
    } catch {
      return nextResolve(`${mapped}/index.ts`, context);
    }
  }
  return nextResolve(mapped, context);
}
