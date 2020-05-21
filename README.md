This file was created with 'atf_norm readme' from files in [txt/](txt/) -- *do not edit*

## Table Of Contents
   * [Orgfile: organize and deduplicate files by timestamp and by contents](#orgfile-organize-and-deduplicate-files-by-timestamp-and-by-contents)
   * [Examples](#examples)
      * [Find files with identical contents (but don't do anything with them)](#find-files-with-identical-contents--but-don-t-do-anything-with-them-)
; [Delete files in secondary backup that already exist in primary backup](#delete-files-in-secondary-backup-that-already-exist-in-primary-backup); [Organize images by year and day](#organize-images-by-year-and-day)

## Orgfile: organize and deduplicate files by timestamp and by contents

Orgfile reads a list of filenames from stdin and performs move and dedup operations with
incoming files. 
This tool is based on openacr and can be merged into any clone of openacr, or compiled stand-alone
with `make`.

If `-commit` is specified, the operation is performed. Otherwise, the description of intended
operation is printed to stdout and no action occurs.
With `-undo`, orgfile reads the output of a previous run (works only with `-move`, not with `-dedup`)
and moves files back to original location. Note, this can still result in data loss because the original move
operation is not a bijection.

If `-move` is specified, then each incoming file is moved into directory
specified by `-tgtdir`. 

If the target file exists, a suffix "-2", "-3", etc is appended 
to the incoming file's basename (so, `a.txt` becomes `a-2.txt`).
If the target file exists and has the same checksum, the move is allowed without rename.

Optionally, the file can be moved into a subdirectory of tgtdir specified with `-subdir`.
`-bydate` is an alias to `-subdir:%Y/%Y-%m-%d`. The file timestamp is determined 
using three methods, in order:
- First, the parent directory of the file is checked for any pattern specified in table `dev.timefmt` marked `dirname:Y`.
(For instance, `"%Y-%m-%d"`)
- If this doesn't yield a timestamp, then the filename is checked for any pattern from `timefmt` table marked `dirname:N`.
Any number of patterns can be provided in the table. Default timefmts support photoshop image name formats.
- If this doesn't yield a timestamp, then the file modification time is used.
Note, use of formatting specifiers other than `%Y,%m,%b,%d` for `-subdir` may
yield zeros.

If `-dedup` is specified, then any incoming file whose pathname matches `-dedup_pathregx` pattern (default %)
is added to an in-memory database of file hashes. If the hash already exists, the incoming file is deleted.

## Examples

### Find files with identical contents (but don't do anything with them)
find . | orgfile -dedup

### Delete files in secondary backup that already exist in primary backup
find backup backup2 -type f | orgfile -dedup -dedup_pathregx "backup2/%" -commit

### Organize images by year and day
find . -name "*.jpg" | orgfile -tgtdir image -bydate -commit

