// expo-crypto under test: node's own getRandomValues, which the native
// module provides on the phone.
export function getRandomValues<T extends ArrayBufferView>(array: T): T {
  return globalThis.crypto.getRandomValues(array as unknown as Uint8Array<ArrayBuffer>) as unknown as T;
}

let minted = 0;
export function randomUUID(): string {
  minted += 1;
  return `00000000-0000-4000-8000-${String(minted).padStart(12, '0')}`;
}
