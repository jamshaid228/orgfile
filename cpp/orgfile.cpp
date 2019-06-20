//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Target: orgfile (exe)
// Exceptions: yes
// Source: cpp/orgfile.cpp
//

#include "include/algo.h"
#include "include/orgfile.h"

// -----------------------------------------------------------------------------

static bool TimeStruct_Read(TimeStruct &ts, strptr str, strptr fmt) {
    StringIter iter(str);
    return algo::TimeStruct_Read(ts,iter,fmt);
}

// -----------------------------------------------------------------------------

static bool ReadParentDirTimestamp(strptr path, TimeStruct &ts) {
    bool ret=false;
    tempstr parentdir(Pathcomp(path,"/RL/RR"));
    if (TimeStruct_Read(ts, parentdir, "%Y_%m_%d")) {// another one
        ret=true;
    } else if (TimeStruct_Read(ts, parentdir, "%Y-%m-%d")) {// another one
        ret=true;
    }
    return ret;
}

// -----------------------------------------------------------------------------

static bool ReadFilenameTimestamp(strptr path, TimeStruct &ts) {
    bool ret=false;
    tempstr name(algo::StripDirName(path));
    if (TimeStruct_Read(ts, name, "PSX_%Y%m%d_")) {// photoshop express format
        ret=true;
    } else if (TimeStruct_Read(ts, name, "signal-%Y-%m-%d-")) {// signal app format
        ret=true;
    } 
    return ret;
}

// -----------------------------------------------------------------------------

// Attempt to determine a photograph's year-month-date from
// its pathname.
// Photos are often stored in directories that look like
//   x/2008_02_03/IMG12343.CRW
// Or
//   x/2008-02-03/IMG12343.CRW
// In this case, extract 2008,02,03 as the date.
// Also check if the filename itself contains the timestamp, such as
// PSX_YYYYmmdd_hhmmss.jpg
//
// If this heuristic doesn't work, use files's modification time.
// (important -- not the creation timestamp; file may have been moved; this
//   changes creation timestamp but keeps the modification timestamp)
static bool GetYMD(strptr path, cstring &year, cstring &month, cstring &day) {
    bool ret=false;
    TimeStruct ts;
    if (ReadParentDirTimestamp(path, ts)) {
        ret=true;
    } else if (ReadFilenameTimestamp(path, ts)) {
        ret=true;
    }
    if (!ret) {// go by the modification date (creation date is unreliable)
        struct stat st;
        ret=stat(Zeroterm(tempstr(path)),&st)==0;
        UnixTime mtime(st.st_mtime);
        ts=algo::GetLocalTimeStruct(mtime);
    }
    if (ret) {
        TimeStruct_Print(ts, year, "%Y");
        TimeStruct_Print(ts, month, "%m");
        TimeStruct_Print(ts, day, "%d");
    }
    return ret;
}

// -----------------------------------------------------------------------------

// Access filename entry for file FNAME.
// Also compute file's hash.
// filename->p_filehash fetches the file hash entry.
// A single filehash may have multiple filenames associated with it.
orgfile::FFilename *orgfile::AccessFilename(strptr fname) {
    orgfile::FFilename *filename = ind_filename_Find(fname);
    if (!filename) {
        filename = &filename_Alloc();
        tempstr cmd;
        cmd << "sha1";
        cmd << " < ";
        strptr_PrintBash(fname,cmd);
        filename->filename = fname;
        filename->filehash = SysEval(cmd,FailokQ(true),1024);
        // cascade create filehash
        ind_filehash_GetOrCreate(filename->filehash);
        vrfy(filename_XrefMaybe(*filename),algo_lib::_db.errtext);
    }
    return filename;
}

// -----------------------------------------------------------------------------

// Use shell to tilde-expand a filename
// ~user/dir -> /home/user/dir
static tempstr TildeExpand(strptr str) {
    tempstr ret;
    if (str.n_elems>0 && str.elems[0]=='~') {
        ret << Trimmed(SysEval(tempstr()<<"echo "<<str,FailokQ(true),1024*4));
    } else {
        ret << str;
    }
    return ret;
}

// -----------------------------------------------------------------------------

// Determine new filename for FNAME.
// If -bydate was specified, the new path is
//   tgtdir/YYYY-mm-dd/<filename>
// Otherwise, it is just
//   tgtdir/<filename>
tempstr orgfile::GetTgtFname(strptr pathname) {
    tempstr tgtdir(_db.cmdline.tgtdir);
    if (_db.cmdline.bydate) {// organizing by date?
        tempstr year,month,day;
        if (GetYMD(pathname,year,month,day)) {
            tgtdir = DirFileJoin(tgtdir, year)<<"/"<<year<<"-"<<month<<"-"<<day;
        }
    }
    tempstr filename(StripDirName(pathname));
    return DirFileJoin(tgtdir,filename);
}

// -----------------------------------------------------------------------------

// Read filenames from STDIN.
// For each file, compute its file hash.
// Delete file file if it's a duplicate (and -commit was specified)
void orgfile::DedupFiles() {
    ind_beg(algo::FileLine_curs,pathname,algo::Fildes(0)) if (FileQ(pathname)) {
        orgfile::FFilename *srcfilename = AccessFilename(pathname);
        if (Regx_Match(_db.cmdline.dedup_pathregx, pathname)) {
            if (c_filename_N(*srcfilename->p_filehash) > 1) {// can dedup?
                prlog("orgfile.dedup"
                      <<Keyval("original",c_filename_Find(*srcfilename->p_filehash,0)->filename)
                      <<Keyval("duplicate",pathname)
                      <<Keyval("comment","contents are identical (based ond hash)"));
                if (_db.cmdline.commit) {// do dedup
                    if (DeleteFile(srcfilename->filename)) {
                        filename_Delete(*srcfilename);
                    }
                }
            }
        }
    }ind_end;
}

// -----------------------------------------------------------------------------

// Move file from FROM to TO
// Use the move command as it can transfer files across filesystem boundaries
static int SystemMv(algo::strptr from, algo::strptr to) {
    CreateDirRecurse(GetDirName(to));
    tempstr cmd;
    cmd<<"mv ";
    strptr_PrintBash(from,cmd);
    cmd<<" ";
    strptr_PrintBash(to,cmd);
    // source and tgt files may be on different filesystems,
    // don't use rename(); use the mv command
    return SysCmd(cmd);
}

// -----------------------------------------------------------------------------

// Move file SRC to TGTFNAME.
// If destination file exists, it is pointed to by TGT.
// If the move succeeds, source entry is deleted to reflect this.
void orgfile::MoveFile(orgfile::FFilename *src, orgfile::FFilename *tgt, strptr tgtfname) {
    vrfy(tgt==NULL || src->filehash==tgt->filehash, "internal error: move with overwrite");
    if (SystemMv(src->filename, tgtfname)) {
        if (!tgt) {
            tgt = &filename_Alloc();
            tgt->filename = tgtfname;
            tgt->filehash = src->filehash;// definitely exists
            vrfy_(filename_XrefMaybe(*tgt));
        }
        // move succeeded, entry no longer needed
        filename_Delete(*src);
    }
}

// -----------------------------------------------------------------------------

// Add suffixes to FNAME until it becomes a suitable
// name for a new file
static tempstr MakeUnique(strptr fname) {
    int index=2;
    cstring ret;
    do {
        ret = tempstr() << StripExt(fname) << "-" << index << GetFileExt(fname);
        index++;
    } while (FileQ(ret));
    return tempstr(ret);
}

// -----------------------------------------------------------------------------
// Read filenames files from STDIN (one per line).
// For each file, determine its new destination by calling GetTgtFname.
// Create new directory structure as appropriate.
// Move the file into place if there was no conflict, or if the file content
//   hash exactly matches
void orgfile::MoveFiles() {
    ind_beg(algo::FileLine_curs,pathname,algo::Fildes(0)) if (FileQ(pathname)) {
        bool canmove=false;
        orgfile::file action;
        action.pathname=pathname;
        if (FileQ(pathname)) {
            action.tgtfile=GetTgtFname(pathname);
        } else {
            action.comment = "file doesn't exist";
        }
        orgfile::FFilename *src = AccessFilename(pathname);
        orgfile::FFilename *tgt = NULL;
        if (action.tgtfile != "" && action.tgtfile != pathname) {
            if (!FileQ(action.tgtfile)) {
                canmove = true;
                action.comment = "move file";
            } else {
                // only if it's known to exist
                tgt = AccessFilename(action.tgtfile);
                canmove = src->filehash == tgt->filehash;
                if (canmove) {
                    action.comment = "move file (proven duplicate)";
                } else {
                    action.comment = "move file (renaming for uniqueness)";
                    action.tgtfile = MakeUnique(action.tgtfile);
                    // how could this possibly return anything except NULL?
                    tgt = ind_filename_Find(action.tgtfile);
                    canmove = true;
                }
            }
            prlog(action);
            if (canmove && _db.cmdline.commit) {// do move
                MoveFile(src,tgt,action.tgtfile);
            }
        }
    }ind_end;
}

// -----------------------------------------------------------------------------

// No hashes are created during this operation.
// Just read orgfile.file records on stdin and move files back
// from TGTFILE -> PATHNAME
void orgfile::Undo() {
    ind_beg(algo::FileLine_curs,line,algo::Fildes(0)) {
        orgfile::file action;
        if (file_ReadStrptrMaybe(action,line)) {
            TSwap(action.pathname, action.tgtfile);
            bool canmove=false;
            if (FileQ(action.pathname)) {// source
                action.comment = "move file back";
                canmove=true;
            } else {
                action.comment = "original not found";
            }
            prlog(action);
            if (canmove && _db.cmdline.commit) {// do move
                SystemMv(action.pathname, action.tgtfile);
            }
        }
    }ind_end;
}

// -----------------------------------------------------------------------------

void orgfile::Main() {
    _db.cmdline.tgtdir = TildeExpand(_db.cmdline.tgtdir);
    vrfy(DirectoryQ(_db.cmdline.tgtdir),
         tempstr()<<"orgfile.baddir"
         <<Keyval("tgtdir",_db.cmdline.tgtdir)
         <<Keyval("comment", "directory doesn't seem to exist"));

    if (_db.cmdline.undo) {
        Undo();
    } else if (_db.cmdline.move) {
        MoveFiles();
    } else if (_db.cmdline.dedup) {
        DedupFiles();
    } else {
        prlog("please specify a command");
        algo_lib::_db.exit_code=1;
    }
}
