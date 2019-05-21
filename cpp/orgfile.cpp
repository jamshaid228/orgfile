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

// Attempt to determine a photograph's year-month-date from
// its pathname.
// Photos are often stored in directories that look like
//   x/2008_02_03/IMG12343.CRW
// Or
//   x/2008-02-03/IMG12343.CRW
// In this case, extract 2008,02,03 as the date.
// If this heuristic doesn't work, use files's modification time.
// (important -- not the creation timestamp; file may have been moved; this
//   changes creation timestamp but keeps the modification timestamp)
static bool GetYMD(strptr filename, cstring &year, cstring &month, cstring &day) {
    bool ret=false;
    tempstr parentdir(Pathcomp(filename,"/RL/RR"));
    TimeStruct ts;
    StringIter iter(parentdir);
    if (TimeStruct_Read(ts, iter, "%Y_%m_%d")) {
        ret=true;
    } else if (TimeStruct_Read(ts, iter, "%Y-%m-%d")) {
        ret=true;
    } else {
        struct stat st;
        ret=stat(Zeroterm(tempstr(filename)),&st)==0;
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
    ind_beg(algo::FileLine_curs,pathname,algo::Fildes(0)) {
        orgfile::FFilename *srcfilename = AccessFilename(pathname);
        if (c_filename_N(*srcfilename->p_filehash) > 1) {// can dedup?
            prlog("orgfile.dedup"
                  <<Keyval("pathname",pathname)
                  <<Keyval("orig",c_filename_Find(*srcfilename->p_filehash,0)->filename)
                  <<Keyval("comment","file is a duplicate"));
            if (_db.cmdline.commit) {// do dedup
                if (DeleteFile(srcfilename->filename)) {
                    filename_Delete(*srcfilename);
                }
            }
        }
    }ind_end;
}

// -----------------------------------------------------------------------------

// Move file SRC to TGTFNAME.
// If destination file exists, it is pointed to by TGT.
// If the move succeeds, source entry is deleted to reflect this.
void orgfile::MoveFile(orgfile::FFilename *src, orgfile::FFilename *tgt, strptr tgtfname) {
    vrfy(tgt==NULL || src->filehash==tgt->filehash, "internal error: move with overwrite");
    CreateDirRecurse(GetDirName(tgtfname));
    tempstr cmd;
    cmd<<"mv ";
    strptr_PrintBash(src->filename,cmd);
    cmd<<" ";
    strptr_PrintBash(tgtfname,cmd);
    // source and tgt files may be on different filesystems,
    // don't use rename(); use the mv command
    if (SysCmd(cmd)==0) {
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
// Read filenames files from STDIN (one per line).
// For each file, determine its new destination by calling GetTgtFname.
// Create new directory structure as appropriate.
// Move the file into place if there was no conflict, or if the file content
//   hash exactly matches
void orgfile::MoveFiles() {
    ind_beg(algo::FileLine_curs,pathname,algo::Fildes(0)) {
        bool canmove=false;
        tempstr comment;
        tempstr tgtfile;
        if (FileQ(pathname)) {
            tgtfile=GetTgtFname(pathname);
        } else {
            comment = "file doesn't exist";
        }
        orgfile::FFilename *src = AccessFilename(pathname);
        orgfile::FFilename *tgt = NULL;
        if (tgtfile != "" && tgtfile != pathname) {
            if (!FileQ(tgtfile)) {
                canmove = true;
                comment = "move file";
            } else {
                // only if it's known to exist
                tgt = AccessFilename(tgtfile);
                canmove = src->filehash == tgt->filehash;
                comment = "move file (proven duplicate)";
            }
            prlog("orgfile.file"
                  <<Keyval("pathname",pathname)
                  <<Keyval("tgtfile",tgtfile)
                  <<Keyval("comment",comment));
            if (canmove && _db.cmdline.commit) {// do move
                MoveFile(src,tgt,tgtfile);
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

    if (_db.cmdline.move) {
        MoveFiles();
    } else if (_db.cmdline.dedup) {
        DedupFiles();
    } else {
        prlog("please specify a command");
        algo_lib::_db.exit_code=1;
    }
}
