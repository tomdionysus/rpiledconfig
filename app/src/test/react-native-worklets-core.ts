// react-native-worklets-core under test: a worklet is an ordinary function here.
export function useRunOnJS<T extends (...args: never[]) => unknown>(fn: T): T {
  return fn;
}
export const Worklets = {
  createRunOnJS<T extends (...args: never[]) => unknown>(fn: T): T {
    return fn;
  },
};
