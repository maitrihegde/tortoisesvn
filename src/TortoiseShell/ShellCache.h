// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2010 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#pragma once
#include "registry.h"
#include "Globals.h"
#include "SVNAdminDir.h"
#include <shlobj.h>

#define REGISTRYTIMEOUT 2000
#define EXCLUDELISTTIMEOUT 5000
#define ADMINDIRTIMEOUT 10000
#define DRIVETYPETIMEOUT 300000		// 5 min
#define NUMBERFMTTIMEOUT 300000
#define MENUTIMEOUT 100

typedef CComCritSecLock<CComCriticalSection> Locker;

/**
 * \ingroup TortoiseShell
 * Helper class which caches access to the registry. Also provides helper methods
 * for checks against the settings stored in the registry.
 */
class ShellCache
{
public:
	enum CacheType
	{
		none,
		exe,
		dll
	};
	ShellCache()
	{
		cachetype = CRegStdDWORD(_T("Software\\TortoiseSVN\\CacheType"), GetSystemMetrics(SM_REMOTESESSION) ? dll : exe);
		showrecursive = CRegStdDWORD(_T("Software\\TortoiseSVN\\RecursiveOverlay"), TRUE);
		folderoverlay = CRegStdDWORD(_T("Software\\TortoiseSVN\\FolderOverlay"), TRUE);
		driveremote = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskRemote"));
		drivefixed = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskFixed"), TRUE);
		drivecdrom = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskCDROM"));
		driveremove = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskRemovable"));
		drivefloppy = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskFloppy"));
		driveram = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskRAM"));
		driveunknown = CRegStdDWORD(_T("Software\\TortoiseSVN\\DriveMaskUnknown"));
		simplecontext = CRegStdDWORD(_T("Software\\TortoiseSVN\\SimpleContext"), FALSE);
		shellmenuaccelerators = CRegStdDWORD(_T("Software\\TortoiseSVN\\ShellMenuAccelerators"), TRUE);
		unversionedasmodified = CRegStdDWORD(_T("Software\\TortoiseSVN\\UnversionedAsModified"), FALSE);
		getlocktop = CRegStdDWORD(_T("Software\\TortoiseSVN\\GetLockTop"), TRUE);
		excludedasnormal = CRegStdDWORD(_T("Software\\TortoiseSVN\\ShowExcludedFoldersAsNormal"), FALSE);
		alwaysextended = CRegStdDWORD(_T("Software\\TortoiseSVN\\AlwaysExtendedMenu"), FALSE);
		cachetypeticker = GetTickCount();
		recursiveticker = cachetypeticker;
		folderoverlayticker = cachetypeticker;
		driveticker = cachetypeticker;
		drivetypeticker = 0;
		langticker = cachetypeticker;
		columnrevformatticker = cachetypeticker;
		pathfilterticker = 0;
		simplecontextticker = cachetypeticker;
		shellmenuacceleratorsticker = cachetypeticker;
		unversionedasmodifiedticker = cachetypeticker;
		admindirticker = cachetypeticker;
		columnseverywhereticker = cachetypeticker;
		getlocktopticker = cachetypeticker;
		excludedasnormalticker = cachetypeticker;
		alwaysextendedticker = cachetypeticker;
		excontextticker = 0;
		menulayoutlow = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntries"), MENUCHECKOUT | MENUUPDATE | MENUCOMMIT);
		menulayouthigh = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntrieshigh"), 0);
		menumasklow_lm = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntriesMaskLow"), 0, FALSE, HKEY_LOCAL_MACHINE);
		menumaskhigh_lm = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntriesMaskHigh"), 0, FALSE, HKEY_LOCAL_MACHINE);
		menumasklow_cu = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntriesMaskLow"), 0);
		menumaskhigh_cu = CRegStdDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntriesMaskHigh"), 0);
		langid = CRegStdDWORD(_T("Software\\TortoiseSVN\\LanguageID"), 1033);
		blockstatus = CRegStdDWORD(_T("Software\\TortoiseSVN\\BlockStatus"), 0);
		columnseverywhere = CRegStdDWORD(_T("Software\\TortoiseSVN\\ColumnsEveryWhere"), FALSE);
		std::fill_n(drivetypecache, 27, (UINT)-1);
		if (DWORD(drivefloppy) == 0)
		{
			// A: and B: are floppy disks
			drivetypecache[0] = DRIVE_REMOVABLE;
			drivetypecache[1] = DRIVE_REMOVABLE;
		}
		TCHAR szBuffer[5];
		columnrevformatticker = GetTickCount();
		SecureZeroMemory(&columnrevformat, sizeof(NUMBERFMT));
		GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, &szDecSep[0], sizeof(szDecSep));
		GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, &szThousandsSep[0], sizeof(szThousandsSep));
		columnrevformat.lpDecimalSep = szDecSep;
		columnrevformat.lpThousandSep = szThousandsSep;
		GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SGROUPING, &szBuffer[0], sizeof(szBuffer)/sizeof(TCHAR));
		columnrevformat.Grouping = _ttoi(szBuffer);
		GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_INEGNUMBER, &szBuffer[0], sizeof(szBuffer)/sizeof(TCHAR));
		columnrevformat.NegativeOrder = _ttoi(szBuffer);
		sAdminDirCacheKey.reserve(MAX_PATH);		// MAX_PATH as buffer reservation ok.
		nocontextpaths = CRegStdString(_T("Software\\TortoiseSVN\\NoContextPaths"), _T(""));
		m_critSec.Init();
	}
	void ForceRefresh()
	{
		cachetype.read();
		showrecursive.read();
		folderoverlay.read();
		driveremote.read();
		drivefixed.read();
		drivecdrom.read();
		driveremove.read();
		drivefloppy.read();
		driveram.read();
		driveunknown.read();
		simplecontext.read();
		shellmenuaccelerators.read();
		unversionedasmodified.read();
		excludedasnormal.read();
		alwaysextended.read();
		menulayoutlow.read();
		menulayouthigh.read();
		langid.read();
		blockstatus.read();
		columnseverywhere.read();
		getlocktop.read();
		menumasklow_lm.read();
		menumaskhigh_lm.read();
		menumasklow_cu.read();
		menumaskhigh_cu.read();
		nocontextpaths.read();

        pathFilter.Refresh();
	}
	CacheType GetCacheType()
	{
		if ((GetTickCount() - cachetypeticker) > REGISTRYTIMEOUT)
		{
			cachetypeticker = GetTickCount();
			cachetype.read();
		}
		return CacheType(DWORD((cachetype)));
	}
	DWORD BlockStatus()
	{
		if ((GetTickCount() - blockstatusticker) > REGISTRYTIMEOUT)
		{
			blockstatusticker = GetTickCount();
			blockstatus.read();
		}
		return (blockstatus);
	}
	unsigned __int64 GetMenuLayout()
	{
		if ((GetTickCount() - layoutticker) > REGISTRYTIMEOUT)
		{
			layoutticker = GetTickCount();
			menulayoutlow.read();
			menulayouthigh.read();
		}
		unsigned __int64 temp = unsigned __int64(DWORD(menulayouthigh))<<32;
		temp |= unsigned __int64(DWORD(menulayoutlow));
		return temp;
	}
	unsigned __int64 GetMenuMask()
	{
		if ((GetTickCount() - menumaskticker) > REGISTRYTIMEOUT)
		{
			menumaskticker = GetTickCount();
			menumasklow_lm.read();
			menumaskhigh_lm.read();
			menumasklow_cu.read();
			menumaskhigh_cu.read();
		}
		DWORD low = (DWORD)menumasklow_lm | (DWORD)menumasklow_cu;
		DWORD high = (DWORD)menumaskhigh_lm | (DWORD)menumaskhigh_cu;
		unsigned __int64 temp = unsigned __int64(high)<<32;
		temp |= unsigned __int64(low);
		return temp;
	}
	BOOL IsRecursive()
	{
		if ((GetTickCount() - recursiveticker)>REGISTRYTIMEOUT)
		{
			recursiveticker = GetTickCount();
			showrecursive.read();
		}
		return (showrecursive);
	}
	BOOL IsFolderOverlay()
	{
		if ((GetTickCount() - folderoverlayticker)>REGISTRYTIMEOUT)
		{
			folderoverlayticker = GetTickCount();
			folderoverlay.read();
		}
		return (folderoverlay);
	}
	BOOL IsSimpleContext()
	{
		if ((GetTickCount() - simplecontextticker)>REGISTRYTIMEOUT)
		{
			simplecontextticker = GetTickCount();
			simplecontext.read();
		}
		return (simplecontext!=0);
	}
	BOOL HasShellMenuAccelerators()
	{
		if ((GetTickCount() - shellmenuacceleratorsticker)>REGISTRYTIMEOUT)
		{
			shellmenuacceleratorsticker = GetTickCount();
			shellmenuaccelerators.read();
		}
		return (shellmenuaccelerators!=0);
	}
	BOOL IsUnversionedAsModified()
	{
		if ((GetTickCount() - unversionedasmodifiedticker)>REGISTRYTIMEOUT)
		{
			unversionedasmodifiedticker = GetTickCount();
			unversionedasmodified.read();
		}
		return (unversionedasmodified);
	}
	BOOL IsGetLockTop()
	{
		if ((GetTickCount() - getlocktopticker)>REGISTRYTIMEOUT)
		{
			getlocktopticker = GetTickCount();
			getlocktop.read();
		}
		return (getlocktop);
	}
	BOOL ShowExcludedAsNormal()
	{
		if ((GetTickCount() - excludedasnormalticker)>REGISTRYTIMEOUT)
		{
			excludedasnormalticker = GetTickCount();
			excludedasnormal.read();
		}
		return (excludedasnormal);
	}
	BOOL AlwaysExtended()
	{
		if ((GetTickCount() - alwaysextendedticker)>REGISTRYTIMEOUT)
		{
			alwaysextendedticker = GetTickCount();
			alwaysextended.read();
		}
		return (alwaysextended);
	}
	BOOL IsRemote()
	{
		DriveValid();
		return (driveremote);
	}
	BOOL IsFixed()
	{
		DriveValid();
		return (drivefixed);
	}
	BOOL IsCDRom()
	{
		DriveValid();
		return (drivecdrom);
	}
	BOOL IsRemovable()
	{
		DriveValid();
		return (driveremove);
	}
	BOOL IsRAM()
	{
		DriveValid();
		return (driveram);
	}
	BOOL IsUnknown()
	{
		DriveValid();
		return (driveunknown);
	}
	BOOL IsContextPathAllowed(LPCTSTR path)
	{
		Locker lock(m_critSec);
		ExcludeContextValid();
		for (std::vector<tstring>::iterator I = excontextvector.begin(); I != excontextvector.end(); ++I)
		{
			if (I->empty())
				continue;
			if (I->size() && I->at(I->size()-1)=='*')
			{
				tstring str = I->substr(0, I->size()-1);
				if (_tcsnicmp(str.c_str(), path, str.size())==0)
					return FALSE;
			}
			else if (_tcsicmp(I->c_str(), path)==0)
				return FALSE;
		}
		return TRUE;
	}
	BOOL IsPathAllowed(LPCTSTR path)
	{
		ValidatePathFilter();
		Locker lock(m_critSec);
        if (!pathFilter.IsPathAllowed (path))
            return FALSE;

		UINT drivetype = 0;
		int drivenumber = PathGetDriveNumber(path);
		if ((drivenumber >=0)&&(drivenumber < 25))
		{
			drivetype = drivetypecache[drivenumber];
			if ((drivetype == -1)||((GetTickCount() - drivetypeticker)>DRIVETYPETIMEOUT))
			{
				if ((DWORD(drivefloppy) == 0)&&((drivenumber == 0)||(drivenumber == 1)))
					drivetypecache[drivenumber] = DRIVE_REMOVABLE;
				else
				{
					drivetypeticker = GetTickCount();
					TCHAR pathbuf[MAX_PATH+4];		// MAX_PATH ok here. PathStripToRoot works with partial paths too.
					_tcsncpy_s(pathbuf, MAX_PATH+4, path, MAX_PATH+3);
					PathStripToRoot(pathbuf);
					PathAddBackslash(pathbuf);
					ATLTRACE2(_T("GetDriveType for %s, Drive %d\n"), pathbuf, drivenumber);
					drivetype = GetDriveType(pathbuf);
					drivetypecache[drivenumber] = drivetype;
				}
			}
		}
		else
		{
			TCHAR pathbuf[MAX_PATH+4];		// MAX_PATH ok here. PathIsUNCServer works with partial paths too.
			_tcsncpy_s(pathbuf, MAX_PATH+4, path, MAX_PATH+3);
			if (PathIsUNCServer(pathbuf))
				drivetype = DRIVE_REMOTE;
			else
			{
				PathStripToRoot(pathbuf);
				PathAddBackslash(pathbuf);
				if (_tcsncmp(pathbuf, drivetypepathcache, MAX_PATH-1)==0)		// MAX_PATH ok.
					drivetype = drivetypecache[26];
				else
				{
					ATLTRACE2(_T("GetDriveType for %s\n"), pathbuf);
					drivetype = GetDriveType(pathbuf);
					drivetypecache[26] = drivetype;
					_tcsncpy_s(drivetypepathcache, MAX_PATH, pathbuf, MAX_PATH);			// MAX_PATH ok.
				} 
			}
		}
		if ((drivetype == DRIVE_REMOVABLE)&&(!IsRemovable()))
			return FALSE;
		if ((drivetype == DRIVE_FIXED)&&(!IsFixed()))
			return FALSE;
		if (((drivetype == DRIVE_REMOTE)||(drivetype == DRIVE_NO_ROOT_DIR))&&(!IsRemote()))
			return FALSE;
		if ((drivetype == DRIVE_CDROM)&&(!IsCDRom()))
			return FALSE;
		if ((drivetype == DRIVE_RAMDISK)&&(!IsRAM()))
			return FALSE;
		if ((drivetype == DRIVE_UNKNOWN)&&(IsUnknown()))
			return FALSE;

		return TRUE;
	}
	DWORD GetLangID()
	{
		if ((GetTickCount() - langticker) > REGISTRYTIMEOUT)
		{
			langticker = GetTickCount();
			langid.read();
		}
		return (langid);
	}
	NUMBERFMT * GetNumberFmt()
	{
		if ((GetTickCount() - columnrevformatticker) > NUMBERFMTTIMEOUT)
		{
			TCHAR szBuffer[5];
			columnrevformatticker = GetTickCount();
			SecureZeroMemory(&columnrevformat, sizeof(NUMBERFMT));
			GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, &szDecSep[0], sizeof(szDecSep));
			GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, &szThousandsSep[0], sizeof(szThousandsSep));
			columnrevformat.lpDecimalSep = szDecSep;
			columnrevformat.lpThousandSep = szThousandsSep;
			GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SGROUPING, &szBuffer[0], sizeof(szBuffer)/sizeof(TCHAR));
			columnrevformat.Grouping = _ttoi(szBuffer);
			GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_INEGNUMBER, &szBuffer[0], sizeof(szBuffer)/sizeof(TCHAR));
			columnrevformat.NegativeOrder = _ttoi(szBuffer);
		}
		return &columnrevformat;
	}
	BOOL HasSVNAdminDir(LPCTSTR path, BOOL bIsDir)
	{
        tstring folder (path);
		if (! bIsDir)
		{
            size_t pos = folder.rfind ('\\');
            if (pos != tstring::npos)
                folder.erase (pos);
		}
		if ((GetTickCount() - admindirticker) < ADMINDIRTIMEOUT)
		{
			std::map<tstring, BOOL>::iterator iter;
			sAdminDirCacheKey = folder;
			if ((iter = admindircache.find(sAdminDirCacheKey)) != admindircache.end())
				return iter->second;
		}

        BOOL hasAdminDir = g_SVNAdminDir.HasAdminDir (folder.c_str(), true);
		admindirticker = GetTickCount();
		Locker lock(m_critSec);
		admindircache[folder] = hasAdminDir;
		return hasAdminDir;
	}
	bool IsColumnsEveryWhere()
	{
		if ((GetTickCount() - columnseverywhereticker) > REGISTRYTIMEOUT)
		{
			columnseverywhereticker = GetTickCount();
			columnseverywhere.read();
		} 
		return !!(DWORD)columnseverywhere;
	}
private:
	void DriveValid()
	{
		if ((GetTickCount() - driveticker)>REGISTRYTIMEOUT)
		{
			driveticker = GetTickCount();
			driveremote.read();
			drivefixed.read();
			drivecdrom.read();
			driveremove.read();
			drivefloppy.read();
		}
	}
	void ExcludeContextValid()
	{
		if ((GetTickCount() - excontextticker)>EXCLUDELISTTIMEOUT)
		{
			Locker lock(m_critSec);
			excontextticker = GetTickCount();
			nocontextpaths.read();
			if (excludecontextstr.compare((tstring)nocontextpaths)==0)
				return;
			excludecontextstr = (tstring)nocontextpaths;
			excontextvector.clear();
			size_t pos = 0, pos_ant = 0;
			pos = excludecontextstr.find(_T("\n"), pos_ant);
			while (pos != tstring::npos)
			{
				tstring token = excludecontextstr.substr(pos_ant, pos-pos_ant);
				if (!token.empty())
					excontextvector.push_back(token);
				pos_ant = pos+1;
				pos = excludecontextstr.find(_T("\n"), pos_ant);
			}
			if (!excludecontextstr.empty())
			{
				tstring token = excludecontextstr.substr(pos_ant, excludecontextstr.size()-1);
				if (!token.empty())
					excontextvector.push_back(token);
			}
			excludecontextstr = (tstring)nocontextpaths;
		}
	}
	void ValidatePathFilter()
	{
        DWORD ticks = GetTickCount();
		if ((ticks - pathfilterticker) > EXCLUDELISTTIMEOUT)
		{
			Locker lock(m_critSec);

            pathfilterticker = ticks;
            pathFilter.Refresh();
		}
	}

    class CPathFilter
    {
    public:

        /// node in the lookup tree

        struct SEntry
        {
            tstring path;

            /// default (path spec did not end a '?').
            /// if this is not set, the default for all
            /// sub-paths is !included.
            /// This is a temporary setting an be invalid
            /// after @ref PostProcessData

            bool recursive;

            /// this is an "include" specification

            bool included;

            /// if @ref recursive is not set, this is
            /// the parent path status being passed down
            /// combined with the information of other
            /// entries for the same @ref path.

            bool subPathIncluded;

            /// do entries for sub-paths exist?

            bool hasSubFolderEntries;

            /// STL support
            /// For efficient folding, it is imperative that
            /// "recursive" entries are first

            bool operator<(const SEntry& rhs) const
            {
                int diff = _tcsicmp (path.c_str(), rhs.path.c_str());
                return (diff < 0) 
                    || ((diff == 0) && recursive && !rhs.recursive);
            }

            friend bool operator< 
                ( const SEntry& rhs
                , const std::pair<LPCTSTR, size_t>& lhs);
            friend bool operator< 
                ( const std::pair<LPCTSTR
                , size_t>& lhs, const SEntry& rhs);
        };

    private:

        /// lookup by path (all entries sorted by path)

        typedef std::vector<SEntry> TData;
        TData data;

        /// registry keys plus cached last content

        CRegStdString excludelist;
	    tstring excludeliststr;

	    CRegStdString includelist;
	    tstring includeliststr;

        /// construct \ref data content

        void AddEntry (const tstring& s, bool include)
        {
            if (s.empty())
                return;

            TCHAR lastChar = *s.rbegin();

            SEntry entry;
            entry.recursive = lastChar != _T('?');
            entry.included = include;
            entry.subPathIncluded = include == entry.recursive;
            entry.hasSubFolderEntries = false;
            entry.path = s;
            if ((lastChar == _T('?')) || (lastChar == _T('*')))
                entry.path.erase (s.length()-1);
            if (!entry.path.empty() && (*entry.path.rbegin() == _T('\\')))
                entry.path.erase (entry.path.length()-1);

            data.push_back (entry);
        }

        void AddEntries (const tstring& s, bool include)
        {
			size_t pos = 0, pos_ant = 0;
			pos = s.find(_T('\n'), pos_ant);
			while (pos != tstring::npos)
			{
				AddEntry (s.substr(pos_ant, pos-pos_ant), include);
				pos_ant = pos+1;
				pos = s.find(_T('\n'), pos_ant);
			}

            if (!s.empty())
                AddEntry (s.substr(pos_ant, s.size()-1), include);
        }

        /// for all paths, have at least one entry in data

        void PostProcessData()
        {
            if (data.empty())
                return;

            std::sort (data.begin(), data.end());

            // update subPathIncluded props and remove duplicate entries

            TData::iterator begin = data.begin();
            TData::iterator end = data.end();
            TData::iterator dest = begin;
            for (TData::iterator source = begin; source != end; ++source)
            {
                if (_tcsicmp (source->path.c_str(), dest->path.c_str()) == 0)
                {
                    // update subPathIncluded
                    // (all relevant parent info has already been normalized)

                    if (!source->recursive)
                        source->subPathIncluded 
                            = IsPathAllowed (source->path.c_str(), begin, dest);

                    // multiple specs for the same path
                    // -> merge them into the existing entry @ dest

                    if (!source->recursive && dest->recursive)
                    {
                        // reset the marker for the this case

                        dest->recursive = false;
                        dest->included = source->included;
                    }
                    else
                    {
                        // include beats exclude

                        dest->included |= source->included;
                        if (source->recursive)
                            dest->subPathIncluded |= source->subPathIncluded;
                    }
                }
                else
                {
                    // new path -> don't merge this entry

                    size_t destSize = dest->path.size();
                    dest->hasSubFolderEntries 
                        =   (source->path.size() > destSize)
                         && (source->path[destSize] == _T('\\'))
                         && (_tcsnicmp ( source->path.substr (0, destSize).c_str()
                                       , dest->path.c_str()
                                       , destSize)
                             == 0);

                    *++dest = *source;

                    // update subPathIncluded
                    // (all relevant parent info has already been normalized)

                    if (!dest->recursive)
                        dest->subPathIncluded 
                            = IsPathAllowed (source->path.c_str(), begin, dest);
                }
            }

            // remove duplicate info

            data.erase (++dest, end);
        }

        /// lookup. default result is "true".
        /// We must look for *every* parent path because of situations like:
        /// excluded: C:, C:\some\deep\path
        /// include: C:\some\path
        /// lookup for C:\some\deeper\path

        bool IsPathAllowed 
            ( LPCTSTR path
            , TData::const_iterator begin
            , TData::const_iterator end) const
        {
            bool result = true;

            // handle special cases

            if (begin == end)
                return result;

            size_t maxLength = _tcslen (path);
            if (maxLength == 0)
                return result;
            
            // look for the most specific entry, start at the root

            size_t pos = 0;
            do
            {
                LPCTSTR backslash = _tcschr (path + pos + 1, _T ('\\'));
                pos = backslash == NULL ? maxLength : backslash - path;

                std::pair<LPCTSTR, size_t> toFind (path, pos);
                TData::const_iterator iter 
                    = std::lower_bound (begin, end, toFind);

                // found a relevant entry?

                if (   (iter != end) 
                    && (iter->path.length() == pos)
                    && (_tcsnicmp (iter->path.c_str(), path, pos) == 0))
                {
                    // exact match?

                    if (pos == maxLength)
                        return iter->included;

                    // parent match

                    result = iter->subPathIncluded;

                    // done?

                    if (iter->hasSubFolderEntries)
                        begin = iter;
                    else
                        return result;
                }
                else
                {
                    // set a (potentially) closer lower limit

                    if (iter != begin)
                        begin = --iter;
                }

                // set a (potentially) closer upper limit

                end = std::upper_bound (begin, end, toFind);
            }
            while ((pos < maxLength) && (begin != end));

            // nothing more specific found

            return result;
        }

    public:

        /// construction

        CPathFilter()
            : excludelist (_T("Software\\TortoiseSVN\\OverlayExcludeList"))
            , includelist (_T("Software\\TortoiseSVN\\OverlayIncludeList"))
        {
            Refresh();
        }
        
        /// notify of (potential) registry settings

        void Refresh()
        {
			excludelist.read();
			includelist.read();

			if (   (excludeliststr.compare ((tstring)excludelist)==0)
                && (includeliststr.compare ((tstring)includelist)==0))
            {
				return;
            }

			excludeliststr = (tstring)excludelist;
			includeliststr = (tstring)includelist;
            AddEntries (excludeliststr, false);
            AddEntries (includeliststr, true);

            PostProcessData();
        }

        /// data access

    	bool IsPathAllowed (LPCTSTR path) const
        {
            return (path != NULL)
                && IsPathAllowed (path, data.begin(), data.end());
        }
    };

    friend bool operator< (const CPathFilter::SEntry& rhs, const std::pair<LPCTSTR, size_t>& lhs);
    friend bool operator< (const std::pair<LPCTSTR, size_t>& lhs, const CPathFilter::SEntry& rhs);

	CRegStdDWORD cachetype;
	CRegStdDWORD blockstatus;
	CRegStdDWORD langid;
	CRegStdDWORD showrecursive;
	CRegStdDWORD folderoverlay;
	CRegStdDWORD getlocktop;
	CRegStdDWORD driveremote;
	CRegStdDWORD drivefixed;
	CRegStdDWORD drivecdrom;
	CRegStdDWORD driveremove;
	CRegStdDWORD drivefloppy;
	CRegStdDWORD driveram;
	CRegStdDWORD driveunknown;
	CRegStdDWORD menulayoutlow;
	CRegStdDWORD menulayouthigh;
	CRegStdDWORD simplecontext;
	CRegStdDWORD shellmenuaccelerators;
	CRegStdDWORD menumasklow_lm;
	CRegStdDWORD menumaskhigh_lm;
	CRegStdDWORD menumasklow_cu;
	CRegStdDWORD menumaskhigh_cu;
	CRegStdDWORD unversionedasmodified;
	CRegStdDWORD excludedasnormal;
	CRegStdDWORD alwaysextended;
	CRegStdDWORD columnseverywhere;

    CPathFilter pathFilter;

	DWORD cachetypeticker;
	DWORD recursiveticker;
	DWORD folderoverlayticker;
	DWORD getlocktopticker;
	DWORD driveticker;
	DWORD drivetypeticker;
	DWORD layoutticker;
	DWORD menumaskticker;
	DWORD langticker;
	DWORD blockstatusticker;
	DWORD columnrevformatticker;
	DWORD pathfilterticker;
	DWORD simplecontextticker;
	DWORD shellmenuacceleratorsticker;
	DWORD unversionedasmodifiedticker;
	DWORD excludedasnormalticker;
	DWORD alwaysextendedticker;
	DWORD columnseverywhereticker;
	UINT  drivetypecache[27];
	TCHAR drivetypepathcache[MAX_PATH];		// MAX_PATH ok.
	NUMBERFMT columnrevformat;
	TCHAR szDecSep[5];
	TCHAR szThousandsSep[5];
	std::map<tstring, BOOL> admindircache;
	tstring sAdminDirCacheKey;
	CRegStdString nocontextpaths;
	tstring excludecontextstr;
	std::vector<tstring> excontextvector;
	DWORD excontextticker;
	DWORD admindirticker;
	CComCriticalSection m_critSec;
};

inline bool operator< 
    ( const ShellCache::CPathFilter::SEntry& lhs
    , const std::pair<LPCTSTR, size_t>& rhs)
{
    return _tcsnicmp (lhs.path.c_str(), rhs.first, rhs.second) < 0;
}

inline bool operator< 
    ( const std::pair<LPCTSTR, size_t>& lhs
    , const ShellCache::CPathFilter::SEntry& rhs)
{
    return _tcsnicmp (lhs.first, rhs.path.c_str(), lhs.second) < 0;
}
