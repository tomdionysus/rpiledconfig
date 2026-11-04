// Every recording in this folder is a decoder test: the decoder must read the
// ID in `expected` from it (within `expected.withinMs` of the first sample, if
// given) and must never read any other ID. A recording saved on a phone (the
// scan screen in a development build) becomes a test by copying it here and
// filling in `expected.idHex` from `rpiledconfig --show-id` on the Pi.
import { describe, expect, it } from 'vitest';
import { replay } from '../recording';
import { loadFixtures } from './load';

const fixtures = loadFixtures();

describe('LED recordings in src/led/fixtures', () => {
  it('has recordings to run', () => {
    expect(fixtures.length).toBeGreaterThan(0);
  });

  for (const fixture of fixtures) {
    it(`decodes ${fixture.file}`, () => {
      expect(fixture.error, `${fixture.file} is not a recording`).toBeUndefined();
      const recording = fixture.recording!;
      expect(recording.expected, `${fixture.file} has no "expected": add { "idHex": "<rpiledconfig --show-id>" } to make it a test`).toBeDefined();
      const expected = recording.expected!;
      const { ids, readAtMs } = replay(recording);

      if (expected.idHex === null) {
        expect(ids, `${fixture.file} should yield no ID`).toEqual([]);
        return;
      }
      expect(ids, `${fixture.file}: IDs read`).toEqual([expected.idHex]);
      if (expected.withinMs !== undefined) expect(readAtMs, `${fixture.file}: read at`).toBeLessThanOrEqual(expected.withinMs);
    });
  }
});
