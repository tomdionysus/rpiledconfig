// Reads every recording in this folder, for the decoder's fixture test. Node
// only: the app never imports this.
import { readdirSync, readFileSync } from 'node:fs';
import { parseRecording, type Recording } from '../recording';

/** Relative to app/, where vitest runs. */
export const FIXTURES_DIR = 'src/led/fixtures';

export interface Fixture {
  file: string;
  recording?: Recording;
  /** Why the file could not be read as a recording. */
  error?: string;
}

export function loadFixtures(dir = FIXTURES_DIR): Fixture[] {
  return readdirSync(dir)
    .filter((file) => file.endsWith('.json'))
    .sort()
    .map((file) => {
      try {
        return { file, recording: parseRecording(JSON.parse(readFileSync(`${dir}/${file}`, 'utf8'))) };
      } catch (error) {
        return { file, error: error instanceof Error ? error.message : String(error) };
      }
    });
}
