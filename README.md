# zfs-rewrite-resume
A resumable directory traversal wrapper for zfs rewrite.

While standard zfs rewrite supports recursion (-r), it cannot resume if interrupted. This tool wraps the command, performing a Breadth-First Search (BFS) traversal of your directories as zfs-rewrite does, allowing you to skip ahead to a specific file if your rewrite operation is stopped (e.g., due to a crash, reboot, or cancellation).

---

## Prerequisites

**Requires a patched ZFS version.**  
`zfs rewrite` is **not** part of upstream OpenZFS. It appears only in certain development branches or custom ZFS builds.

---

## Installation

This tool is a single C file with no external dependencies.

Compile with:

```bash
gcc zfs-rewrite-resume.c -o zfs-rewrite-resume
```

---

## Usage

```bash
./zfs-rewrite-resume [OPTIONS] <directory|file...>
```

This wrapper automatically recurses into directories (similar to `zfs rewrite -r`).

---

## Wrapper Control Options

| Flag        | Description |
|-------------|-------------|
| `-c <file>` | **Continue/Resume**. Full path to the file to resume from. Traversal skips executing rewrites until this file is reached. |
| `-n`        | **Dry Run**. Traverse directories without rewriting. With `-c`, exits immediately upon finding the resume file. |
| `-h`        | Display help message. |

---

## ZFS Rewrite Options

These pass directly through to the underlying `zfs rewrite` logic.

| Flag        | Description |
|-------------|-------------|
| `-v`        | **Verbose**. Print each rewritten file (required to know where to resume). |
| `-x`        | Stay within the same filesystem; do not cross mountpoints. |
| `-l <len>`  | Rewrite at most this many bytes. |
| `-o <off>`  | Start rewriting at this byte offset. |

> Recursion is implied when passing a directory — no need for `-r`.

---

## Perform a dry run first to confirm everything works as expected
Before attempting a resumed rewrite (`-c <file>`), its a good idea to confirm that:

1. The resume file exists  
2. The tool can locate it in the traversal  
3. The path is correct and matches the logged output  

A dry run prevents accidental rewrites of the wrong subtree or dataset.

Example:

```bash
./zfs-rewrite-resume -n -v -c "/tank/data/images/photo003.jpg" /tank/data
```

Expected output:

```
INFO: Dry run successful. Found resume point and will now exit.
```

If the dry run **does not** find the resume point, **stop** and do not continue.

---

## Workflow: Resuming an Interrupted Rewrite

### 1. Start the Rewrite

Use `-v` and redirect output to a log:

```bash
./zfs-rewrite-resume -v /tank/data | tee -a rewrite.log
```

---

### 2. Interruption Occurs

Check the last entry in the log:

```
/tank/data/images/photo001.jpg
/tank/data/images/photo002.jpg
/tank/data/images/photo003.jpg  <-- Last processed file
```

---

### 3. Resume

Use the last successfully processed file with `-c`:

```bash
./zfs-rewrite-resume -v -c "/tank/data/images/photo003.jpg" /tank/data >> rewrite.log
```

The tool will skip all earlier files and resume rewriting at this point.

---

## License

Open source — use at your own risk.  
**Always keep backups before rewriting ZFS blocks.**
