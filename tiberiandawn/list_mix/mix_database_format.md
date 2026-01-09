# OpenRA Global Mix Database Format

## File: `global mix database.dat`

### Structure

**Header:**
- **Offset 0x00-0x03**: `uint32` (little-endian) - Count value (2449 in observed file)
  - Purpose unclear - doesn't match number of entries or filenames exactly

**Data:**
- **Offset 0x04+**: Sequence of null-terminated ASCII strings
- Strings alternate between:
  - **Filenames**: e.g., `a10icnh.tem`, `affirm1.v00`, `a10icon.shp`
  - **Descriptions**: e.g., `icon: a10 warthog`, `c&c #1 affirmative`, `helicopter takeoff`

### Observations

- **File size**: 421,826 bytes
- **Total strings**: 34,481 null-terminated strings
- **Filename-like strings**: 24,567 (contain '.' and don't start with 'icon:')
- **Header count**: 2,449

### Pattern

Each entry appears to consist of:
1. A filename (null-terminated)
2. A description (null-terminated)

Example:
```
a10icnh.tem\0icon: a10 warthog\0a10icnh.win\0icon: a10 warthog\0...
```

### Notes

- The header count (2,449) does not match:
  - Number of filename+description pairs (would be ~17,240)
  - Number of filenames (24,567)
  - Number of strings / 2 (17,240.5)
  
- Possible interpretations:
  - Count might represent number of unique MIX files referenced
  - Count might be a version or generation number
  - Count might index into a specific subset of entries
  - Format might have variable-length entries or additional metadata

### Example Entries

```
Entry 1:
  Filename: "a10icnh.tem"
  Description: "icon: a10 warthog"

Entry 2:
  Filename: "a10icnh.win"
  Description: "icon: a10 warthog"

Entry 3:
  Filename: "affirm1.v00"
  Description: "c&c #1 affirmative"
```

### Python Parser

```python
import struct

def read_mix_database(filename):
    with open(filename, 'rb') as f:
        data = f.read()
    
    # Read header
    count = struct.unpack('<I', data[:4])[0]
    
    # Parse strings
    offset = 4
    entries = []
    
    while offset < len(data):
        null_pos = data.find(b'\x00', offset)
        if null_pos == -1:
            break
        
        string = data[offset:null_pos].decode('ascii')
        offset = null_pos + 1
        
        # Pair up filenames and descriptions
        if '.' in string and not string.startswith('icon:'):
            # This is a filename
            if offset < len(data):
                desc_null = data.find(b'\x00', offset)
                if desc_null != -1:
                    description = data[offset:desc_null].decode('ascii')
                    entries.append({
                        'filename': string,
                        'description': description
                    })
                    offset = desc_null + 1
                    continue
        
        offset = null_pos + 1
    
    return entries
```

