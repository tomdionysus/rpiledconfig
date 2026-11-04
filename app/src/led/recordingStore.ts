// Recordings on the phone: <document directory>/led-recordings/*.json,
// through expo-file-system. Thin; the format and every decision about it are
// in recording.ts. Getting a file off the phone is `exportRecording`: the
// system folder picker (Storage Access Framework on Android, the Files app's
// picker on iOS) and a copy into the folder chosen, so no sharing library is
// needed. On an Android debug build `adb exec-out run-as
// com.tomcully.rpiledconfig cat files/led-recordings/<name>` also works.
import { Directory, File, Paths } from 'expo-file-system';
import { parseRecording, recordingFileName, serialiseRecording, type Recording } from './recording';

const FOLDER = 'led-recordings';

function folder(): Directory {
  const dir = new Directory(Paths.document, FOLDER);
  if (!dir.exists) dir.create({ intermediates: true, idempotent: true });
  return dir;
}

export interface SavedRecording {
  name: string;
  uri: string;
  size: number;
  modified: number | null;
}

export function saveRecording(recording: Recording): SavedRecording {
  const file = new File(folder(), recordingFileName(recording));
  file.write(serialiseRecording(recording));
  return { name: file.name, uri: file.uri, size: file.size, modified: file.lastModified };
}

/** Newest first. */
export function listRecordings(): SavedRecording[] {
  return folder()
    .list()
    .filter((entry): entry is File => entry instanceof File && entry.name.endsWith('.json'))
    .map((file) => ({ name: file.name, uri: file.uri, size: file.size, modified: file.lastModified }))
    .sort((a, b) => (b.modified ?? 0) - (a.modified ?? 0) || (a.name < b.name ? 1 : -1));
}

export async function readRecording(saved: SavedRecording): Promise<Recording> {
  return parseRecording(JSON.parse(await new File(saved.uri).text()));
}

export function deleteRecording(saved: SavedRecording): void {
  const file = new File(saved.uri);
  if (file.exists) file.delete();
}

/** Asks the user for a folder and writes a copy there; resolves to the copy's name, or undefined if they cancelled. */
export async function exportRecording(saved: SavedRecording): Promise<string | undefined> {
  let target: Directory;
  try {
    target = await Directory.pickDirectoryAsync();
  } catch {
    return undefined;
  }
  const text = await new File(saved.uri).text();
  const copy = target.createFile(saved.name, 'application/json');
  copy.write(text);
  return copy.name;
}
