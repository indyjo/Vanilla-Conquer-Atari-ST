# list_mix.py - MIX File Contents Lister

A command-line utility to list the contents of Command & Conquer MIX archive files. The tool displays the CRC (checksum), offset, and size of each file contained in a MIX archive, and optionally identifies filenames and descriptions using an embedded database.

## Features

- Lists all files contained in one or more MIX files
- Shows CRC codes, file offsets, and sizes
- Automatically identifies filenames and descriptions using an embedded database
- Supports multiple MIX files in a single run
- Self-contained (no external database file required by default)

## Usage

```bash
# List contents of a single MIX file (uses embedded database)
python3 list_mix.py file.mix

# List contents of multiple MIX files
python3 list_mix.py file1.mix file2.mix file3.mix

# Use an external database file instead of the embedded one
python3 list_mix.py -d database.dat file1.mix file2.mix

# Show help
python3 list_mix.py --help
```

## Output Format

When a database is available (embedded or external), the output includes:
- Index
- CRC (hexadecimal)
- Offset (bytes)
- Size (bytes)
- Filename (if found in database)
- Description (if found in database)

Files not found in the database are marked as "(unknown)".

## Database

This tool embeds a modified version of OpenRA's global mix database. The database has been:
- Sorted lexicographically by filename for better compression
- Compressed using zlib and encoded with base85
- Embedded directly in the script for convenience

The embedded database contains filename/description mappings for thousands of game files, allowing automatic identification of files in MIX archives by their CRC codes.

## Technical Details

The CRC calculation algorithm matches the original C++ implementation from the Command & Conquer codebase, ensuring compatibility with the game's file identification system.

## Disclaimer

**This tool was AI-generated and is provided as-is as a debugging tool.**

- No warranty is provided
- Use at your own risk
- The embedded database is based on OpenRA's mix database but has been modified (sorted) for compression purposes
- This is a utility tool for analyzing MIX file contents, not an official tool

## License

This tool is provided for debugging and analysis purposes. The embedded database is derived from OpenRA's mix database format.

