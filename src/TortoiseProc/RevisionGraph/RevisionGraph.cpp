// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2008 - TortoiseSVN

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
#include "StdAfx.h"
#include "resource.h"
#include "client.h"
#include "UnicodeUtils.h"
#include "registry.h"
#include "AppUtils.h"
#include "PathUtils.h"
#include "SVN.h"
#include "SVNInfo.h"
#include "TSVNPath.h"
#include ".\revisiongraph.h"
#include "SVNError.h"
#include "CachedLogInfo.h"
#include "RepositoryInfo.h"
#include "RevisionIndex.h"
#include "CopyFollowingLogIterator.h"
#include "StrictLogIterator.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CRevisionGraph::CRevisionGraph(void) : m_bCancelled(FALSE)
	, m_FilterMinRev(-1)
	, m_FilterMaxRev(LONG_MAX)
    , nodePool (sizeof (CRevisionEntry), 1024)
    , m_wcRev (-1)
    , m_pegRev (-1)
{
	memset (&m_ctx, 0, sizeof (m_ctx));
	parentpool = svn_pool_create(NULL);

	Err = svn_config_ensure(NULL, parentpool);
	pool = svn_pool_create (parentpool);
	// set up the configuration
	if (Err == 0)
		Err = svn_config_get_config (&(m_ctx.config), g_pConfigDir, pool);

	if (Err != 0)
	{
		::MessageBox(NULL, this->GetLastErrorMessage(), _T("TortoiseSVN"), MB_ICONERROR);
		svn_error_clear(Err);
		svn_pool_destroy (pool);
		svn_pool_destroy (parentpool);
		exit(-1);
	}

	// set up authentication
	m_prompt.Init(pool, &m_ctx);

	m_ctx.cancel_func = cancel;
	m_ctx.cancel_baton = this;

	//set up the SVN_SSH param
	CString tsvn_ssh = CRegString(_T("Software\\TortoiseSVN\\SSH"));
	if (tsvn_ssh.IsEmpty())
		tsvn_ssh = CPathUtils::GetAppDirectory() + _T("TortoisePlink.exe");
	tsvn_ssh.Replace('\\', '/');
	if (!tsvn_ssh.IsEmpty())
	{
		svn_config_t * cfg = (svn_config_t *)apr_hash_get (m_ctx.config, SVN_CONFIG_CATEGORY_CONFIG,
			APR_HASH_KEY_STRING);
		svn_config_set(cfg, SVN_CONFIG_SECTION_TUNNELS, "ssh", CUnicodeUtils::GetUTF8(tsvn_ssh));
	}
}

CRevisionGraph::~CRevisionGraph(void)
{
	svn_error_clear(Err);
	svn_pool_destroy (parentpool);

	ClearRevisionEntries();
}

void CRevisionGraph::ClearRevisionEntries()
{
	for (size_t i = m_entryPtrs.size(); i > 0; --i)
        m_entryPtrs[i-1]->Destroy (nodePool);

	m_entryPtrs.clear();

	copyToRelation.clear();
	copyFromRelation.clear();
	copiesContainer.clear();
}

bool CRevisionGraph::SetFilter(svn_revnum_t minrev, svn_revnum_t maxrev, const CString& sPathFilter)
{
	m_FilterMinRev = minrev;
	m_FilterMaxRev = maxrev;
	m_filterpaths.clear();
	// the filtered paths are separated by an '*' char, since that char is illegal in paths and urls
	if (!sPathFilter.IsEmpty())
	{
		int pos = sPathFilter.Find('*');
		if (pos)
		{
			CString sTemp = sPathFilter;
			while (pos>=0)
			{
				m_filterpaths.insert(CUnicodeUtils::StdGetUTF8((LPCTSTR)sTemp.Left(pos)));
				sTemp = sTemp.Mid(pos+1);
				pos = sTemp.Find('*');
			}
			m_filterpaths.insert(CUnicodeUtils::StdGetUTF8((LPCTSTR)sTemp));
		}
		else
			m_filterpaths.insert(CUnicodeUtils::StdGetUTF8((LPCTSTR)sPathFilter));
	}
	return true;
}

BOOL CRevisionGraph::ProgressCallback(CString /*text*/, CString /*text2*/, DWORD /*done*/, DWORD /*total*/) {return TRUE;}

svn_error_t* CRevisionGraph::cancel(void *baton)
{
	CRevisionGraph * me = (CRevisionGraph *)baton;
	if (me->m_bCancelled)
	{
		CString temp;
		temp.LoadString(IDS_SVN_USERCANCELLED);
		return svn_error_create(SVN_ERR_CANCELLED, NULL, CUnicodeUtils::GetUTF8(temp));
	}
	return SVN_NO_ERROR;
}

// implement ILogReceiver

void CRevisionGraph::ReceiveLog ( LogChangedPathArray* changes
					            , svn_revnum_t rev
                                , const StandardRevProps* stdRevProps
                                , UserRevPropArray* userRevProps
                                , bool mergesFollow)
{
    // fix release mode compiler warning

    UNREFERENCED_PARAMETER(changes);
    UNREFERENCED_PARAMETER(stdRevProps);
    UNREFERENCED_PARAMETER(userRevProps);
    UNREFERENCED_PARAMETER(mergesFollow);

	// update internal data

	if ((m_lHeadRevision < (revision_t)rev) || (m_lHeadRevision == NO_REVISION))
		m_lHeadRevision = rev;

	// update progress bar and check for user pressing "Cancel" somewhere

	static DWORD lastProgressCall = 0;
	if (lastProgressCall < GetTickCount() - 200)
	{
		lastProgressCall = GetTickCount();

		CString temp, temp2;
		temp.LoadString(IDS_REVGRAPH_PROGGETREVS);
		temp2.Format(IDS_REVGRAPH_PROGCURRENTREV, rev);
		if (!ProgressCallback (temp, temp2, m_lHeadRevision - rev, m_lHeadRevision))
		{
			m_bCancelled = TRUE;
			throw SVNError (cancel (this));
		}
	}
}

BOOL CRevisionGraph::FetchRevisionData (CString path, svn_revnum_t pegRev, const SOptions& /*options*/)
{
	// set some text on the progress dialog, before we wait
	// for the log operation to start
	CString temp;
	temp.LoadString(IDS_REVGRAPH_PROGGETREVS);
	ProgressCallback(temp, _T(""), 0, 1);

	// prepare the path for Subversion
	SVN::preparePath(path);
	CStringA url = CUnicodeUtils::GetUTF8(svn.GetURLFromPath(CTSVNPath(path)));
	url = CPathUtils::PathEscape(url);

	// we have to get the log from the repository root
	CTSVNPath urlpath;
	urlpath.SetFromSVN(url);

	m_sRepoRoot = svn.GetRepositoryRoot(urlpath);
    url = m_sRepoRoot;
	urlpath.SetFromSVN(url);

	CTSVNPath dummy;
    svn_revnum_t headRevision = NO_REVISION;

	if (   (svn.GetRootAndHead (urlpath, dummy, headRevision) == FALSE)
        || m_sRepoRoot.IsEmpty())
	{
		Err = svn_error_dup(svn.Err);
		return FALSE;
	}

	// fix issue #360: use WC revision as peg revision

    m_pegRev = pegRev;
    if (m_pegRev == -1)
    {
	    CTSVNPath svnPath (path);
	    if (!svnPath.IsUrl())
	    {
		    SVNInfo info;
		    const SVNInfoData * baseInfo 
			    = info.GetFirstFileInfo (svnPath, SVNRev(), SVNRev());
            if (baseInfo != NULL)
                m_pegRev = baseInfo->lastchangedrev;
	    }
    }

	// fetch missing data from the repository

    m_lHeadRevision = (revision_t)NO_REVISION;
	try
	{
        // select / construct query object and optimize revision range to fetch

		svnQuery.reset (new CSVNLogQuery (&m_ctx, pool));
        SVNRev firstRevision = 0;
        if (svn.GetLogCachePool()->IsEnabled())
        {
            CLogCachePool* pool = svn.GetLogCachePool();
		    query.reset (new CCacheLogQuery (pool, svnQuery.get()));

            CString uuid = pool->GetRepositoryInfo().GetRepositoryUUID (urlpath);
            firstRevision = pool->GetCache (uuid)->GetRevisions().GetFirstMissingRevision(1);
			// if the cache is already complete, the firstRevision here is
			// HEAD+1 - that revision does not exist and would throw an error later
			if (svn_revnum_t(firstRevision) > svn_revnum_t(headRevision))
				firstRevision = headRevision;
        }
        else
		    query.reset (new CCacheLogQuery (svn, svnQuery.get()));

        // actually fetch the data

		query->Log ( CTSVNPathList (urlpath)
				   , -1
				   , headRevision
				   , firstRevision
				   , 0
				   , false		// strictNodeHistory
				   , this
                   , false		// includeChanges (log cache fetches them automatically)
                   , false		// includeMerges
                   , true		// includeStandardRevProps
                   , false		// includeUserRevProps
                   , TRevPropNames());

        // initialize path classificator

        CRegStdString trunkPattern (_T("Software\\TortoiseSVN\\RevisionGraph\\TrunkPattern"), _T("trunk"));
        CRegStdString branchesPattern (_T("Software\\TortoiseSVN\\RevisionGraph\\BranchPattern"), _T("branches"));
        CRegStdString tagsPattern (_T("Software\\TortoiseSVN\\RevisionGraph\\TagsPattern"), _T("tags"));

        const CPathDictionary& paths = query->GetCache()->GetLogInfo().GetPaths();
        pathClassification.reset 
            (new CPathClassificator ( paths
                                    , CUnicodeUtils::StdGetUTF8 (trunkPattern)
                                    , CUnicodeUtils::StdGetUTF8 (branchesPattern)
                                    , CUnicodeUtils::StdGetUTF8 (tagsPattern)));
	}
	catch (SVNError& e)
	{
		Err = svn_error_create (e.GetCode(), NULL, e.GetMessage());
		return FALSE;
	}

	return TRUE;
}

void CRevisionGraph::AnalyzeRevisionData (CString path, const SOptions& options)
{
	svn_error_clear(Err);
    Err = NULL;

	ClearRevisionEntries();
	m_maxurllength = 0;
	m_maxurl.Empty();
	m_maxRow = 0;
	m_maxColumn = 0;

	SVN::preparePath(path);
	CStringA url = CUnicodeUtils::GetUTF8(svn.GetURLFromPath(CTSVNPath(path)));

	url = CPathUtils::PathUnescape(url);
	url = url.Mid(CPathUtils::PathUnescape(m_sRepoRoot).GetLength());

	m_wcURL = url;

	// find the revision the working copy is on, we mark that revision
	// later in the graph
    // (handle option.showWCRev changes properly!)

    if ((m_wcRev == -1) == options.showWCRev)
    {
	    svn_revnum_t minrev;
	    bool switched, modified, sparse;
	    if (   !options.showWCRev 
		    || !svn.GetWCRevisionStatus ( CTSVNPath(path)
									    , true
									    , minrev
									    , m_wcRev
									    , switched
									    , modified
									    , sparse))
	    {
		    m_wcRev = -1;
	    }
    }

    // special case: empty log

    if (m_lHeadRevision == NO_REVISION)
        return;

    // if we don't have a peg revision yet, set it to HEAD

    if (m_pegRev == -1)
        m_pegRev = m_lHeadRevision;

	// in case our path was renamed and had a different name in the past,
	// we have to find out that name now, because we will analyze the data
	// from lower to higher revisions

	const CCachedLogInfo* cache = query->GetCache();
	const CPathDictionary* paths = &cache->GetLogInfo().GetPaths();
	CDictionaryBasedTempPath startPath (paths, (const char*)url);

	CCopyFollowingLogIterator iterator (cache, m_pegRev, startPath);
	iterator.Retry();
	revision_t initialrev = m_pegRev;

	while (   (iterator.GetRevision() > 0) 
		   && !iterator.EndOfPath()
		   && !iterator.DataIsMissing())
	{
		initialrev = iterator.GetRevision();
		iterator.Advance();
	}

	startPath = iterator.GetPath();

	// step 1: create "copy-to" lists based on the "copy-from" info

	BuildForwardCopies();

	// step 2: crawl the history upward, follow branches and create revision info graph

    AnalyzeRevisions (startPath, initialrev, options);

	// step 3: reduce graph by saying "renamed" instead of "deleted"+"addedWithHistory" etc.

	Optimize (options);

	// step 4: place the nodes on a row, column grid

	AssignCoordinates (options);

	// step 5: final sorting etc.

	Cleanup();
}

inline bool AscendingFromRevision (const SCopyInfo* lhs, const SCopyInfo* rhs)
{
	return lhs->fromRevision < rhs->fromRevision;
}

inline bool AscendingToRevision (const SCopyInfo* lhs, const SCopyInfo* rhs)
{
	return lhs->toRevision < rhs->toRevision;
}

void CRevisionGraph::BuildForwardCopies()
{
	// iterate through all revisions and fill copyToRelation:
	// for every copy-from info found, add an entry

	const CCachedLogInfo* cache = query->GetCache();
	const CRevisionIndex& revisions = cache->GetRevisions();
	const CRevisionInfoContainer& revisionInfo = cache->GetLogInfo();

	// for all revisions ...

	copiesContainer.reserve (revisions.GetLastRevision());
	for ( revision_t revision = revisions.GetFirstRevision()
		, last = revisions.GetLastRevision()
		; revision < last
		; ++revision)
	{
		// ... in the cache ...

		index_t index = revisions[revision];
		if (   (index != NO_INDEX) 
			&& (revisionInfo.GetSumChanges (index) & CRevisionInfoContainer::HAS_COPY_FROM))
		{
			// ... examine all changes ...

			for ( CRevisionInfoContainer::CChangesIterator 
					iter = revisionInfo.GetChangesBegin (index)
				, end = revisionInfo.GetChangesEnd (index)
				; iter != end
				; ++iter)
			{
				// ... and if it has a copy-from info ...

				if (iter->HasFromPath())
				{
					// ... add it to the list

					SCopyInfo copyInfo;

					copyInfo.fromRevision = iter->GetFromRevision();
					copyInfo.fromPathIndex = iter->GetFromPathID();
					copyInfo.toRevision = revision;
					copyInfo.toPathIndex = iter->GetPathID();

					copiesContainer.push_back (copyInfo);
				}
			}
		}
	}

	// sort container by source revision and path

	copyToRelation.resize (copiesContainer.size());
	copyFromRelation.resize (copiesContainer.size());

	for (size_t i = 0, count = copiesContainer.size(); i < count; ++i)
	{
		SCopyInfo *copyInfo = &copiesContainer.at(i);
		copyToRelation[i] = copyInfo;
		copyFromRelation[i] = copyInfo;
	}

	std::sort (copyToRelation.begin(), copyToRelation.end(), &AscendingToRevision);
	std::sort (copyFromRelation.begin(), copyFromRelation.end(), &AscendingFromRevision);
}

inline bool CompareByRevision (CRevisionEntry* lhs, CRevisionEntry* rhs)
{
    return lhs->revision < rhs->revision;
}

void CRevisionGraph::InsertWCRevision (const CDictionaryBasedTempPath& wcPath)
{
    // maybe, we don't have a WC revision

	if (m_wcRev <= 0)
        return;

    // for technical reasons (use of std::upper_bound), 
    // we always create a new entry for the WC rev.
    // We may have to delete it soon after, though.

    CRevisionEntry* newEntry 
		= CRevisionEntry::Create (wcPath, m_wcRev, CRevisionEntry::modified, nodePool);

    // where the new entry belongs within the node list
    // (all sorted by revision)

    typedef std::vector<CRevisionEntry*>::iterator IT;
    IT lowerBound
        = std::lower_bound ( m_entryPtrs.begin()
                           , m_entryPtrs.end()
                           , newEntry
                           , &CompareByRevision);
    IT upperBound
        = std::upper_bound ( m_entryPtrs.begin()
                           , m_entryPtrs.end()
                           , newEntry
                           , &CompareByRevision);

    // node already there?

	for (IT iter = lowerBound; iter < upperBound; ++iter)
	{
		CRevisionEntry* entry = *iter;
        if (entry->path == wcPath)
		{
			entry->bWorkingCopy = true;
            newEntry->Destroy (nodePool);

            return;
		}
	}

    // find previous entry

    for (size_t i = lowerBound - m_entryPtrs.begin(); i > 0; --i)
    {
		CRevisionEntry* entry = m_entryPtrs[i-1];
		if (entry->path == wcPath)
		{
            // insert it right here

            assert (entry->revision < (revision_t)m_wcRev);
            assert (  (entry->next == NULL) 
                    || (entry->next->revision > (revision_t)m_wcRev));

            m_entryPtrs.insert (upperBound, newEntry);

            // link it properly

			newEntry->bWorkingCopy = true;
            newEntry->next = entry->next;
            newEntry->prev = entry;
            entry->next = newEntry;

            if (newEntry->next)
                newEntry->next->prev = newEntry;

            // ready

            return;
        }
    }

    // insertion position not found
    // (maybe, we worked from outdated off-line cache?)

    newEntry->Destroy (nodePool);
}


void CRevisionGraph::AnalyzeRevisions ( const CDictionaryBasedTempPath& path
									  , revision_t startrev
									  , const SOptions& options)
{
	const CCachedLogInfo* cache = query->GetCache();
	const CRevisionIndex& revisions = cache->GetRevisions();
	const CRevisionInfoContainer& revisionInfo = cache->GetLogInfo();

	// initialize the paths we have to search for

	std::auto_ptr<CSearchPathTree> searchTree 
		(new CSearchPathTree (&revisionInfo.GetPaths()));
	searchTree->Insert (path, startrev);

	// the range of copy-to info that applies to the current revision

	TSCopyIterator lastFromCopy = copyFromRelation.begin();
	TSCopyIterator lastToCopy = copyToRelation.begin();

	// collect nodes to draw ... revision by revision

	for (revision_t revision = startrev; revision <= m_lHeadRevision; ++revision)
	{
		index_t index = revisions[revision];
		if (index == NO_INDEX)
			continue;

		// handle remaining copy-to entries
		// (some may have a fromRevision that does not touch the fromPath)

		AddCopiedPaths ( revision
					   , searchTree.get()
					   , lastToCopy);

	    // collect search paths that have been deleted in this container
	    // (delay potential node deletion until we finished tree traversal)

	    std::vector<CSearchPathTree*> toRemove;

		// we are looking for search paths that (may) overlap 
		// with the revisions' changes

		CDictionaryBasedPath basePath = revisionInfo.GetRootPath (index);
		if (basePath.IsValid())
        {
		    // pre-order search-tree traversal

			CSearchPathTree* startNode 
				= searchTree->FindCommonParent (basePath.GetIndex());

			if (startNode->GetPath().IsSameOrChildOf (basePath))
			{
				CSearchPathTree* searchNode = startNode;

				if (   options.includeSubPathChanges
					|| (   revisionInfo.GetSumChanges (index)
						!= CRevisionInfoContainer::ACTION_CHANGED))
				{
					// maybe a hit -> match all changes against the whole sub-tree

					AnalyzeRevisions ( revision
								     , revisionInfo.GetChangesBegin (index)
								     , revisionInfo.GetChangesEnd (index)
								     , searchNode
								     , options.includeSubPathChanges
								     , toRemove);
				}
				else
				{
					// only simple changes, if any

					AnalyzeChangesOnly ( revision
									   , revisionInfo.GetChangesBegin (index)
									   , revisionInfo.GetChangesEnd (index)
									   , searchNode);
				}

				startNode = startNode->GetParent();
			}
			else
			{
				CDictionaryBasedPath commonRoot
					= basePath.GetCommonRoot (startNode->GetPath().GetBasePath());
				startNode = searchTree->FindCommonParent (commonRoot.GetIndex());
			}

			// mark changes on parent search nodes

			for ( CSearchPathTree* searchNode = startNode
				; searchNode != NULL
				; searchNode = searchNode->GetParent())
		    {
				if (searchNode->IsActive())
				{
					if (options.includeSubPathChanges)
					{
						AnalyzeRevisions ( revision
										 , revisionInfo.GetChangesBegin (index)
										 , revisionInfo.GetChangesEnd (index)
										 , searchNode
										 , true
										 , toRemove);
					}
					else
					{
						// this path has been touched in this revision

						searchNode->SetStartRevision (revision);
					}
				}
		    }
		}

		// handle remaining copy-to entries
		// (some may have a fromRevision that does not touch the fromPath)

		FillCopyTargets ( revision
						, searchTree.get()
						, lastFromCopy
                        , options.exactCopySources);

		// remove deleted search paths

		for (size_t i = 0, count = toRemove.size(); i < count; ++i)
			toRemove[i]->Remove();
	}

    // add head revisions, 
    // if requested by options and not already provided by showAll

    if (options.showHEAD && !options.includeSubPathChanges)
    {
        size_t sortedNodeCount = m_entryPtrs.size();

        AddMissingHeads (searchTree.get());

        // move the new nodes to their final positions according to their revnum

        std::sort ( m_entryPtrs.begin() + sortedNodeCount
                  , m_entryPtrs.end()
                  , &CompareByRevision);
        std::inplace_merge ( m_entryPtrs.begin()
                           , m_entryPtrs.begin() + sortedNodeCount
                           , m_entryPtrs.end()
                           , &CompareByRevision);
    }

    // mark the WC revision and path

    CDictionaryBasedTempPath wcPath (path.GetDictionary(), (const char*)m_wcURL);
    InsertWCRevision (wcPath);

    // mark all heads

    if (options.showHEAD)
        MarkHeads (searchTree.get());
}

void CRevisionGraph::AnalyzeRevisions ( revision_t revision
									  , CRevisionInfoContainer::CChangesIterator first
									  , CRevisionInfoContainer::CChangesIterator last
									  , CSearchPathTree* startNode
									  , bool bShowAll
									  , std::vector<CSearchPathTree*>& toRemove)
{
	typedef CRevisionInfoContainer::CChangesIterator IT;

	CSearchPathTree* searchNode = startNode;
	do
	{
		// in many cases, we want only to see additions, 
		// deletions and replacements

		bool skipSubTree = true;

		const CDictionaryBasedTempPath& path = searchNode->GetPath();

		// we must not modify inactive nodes

		if (searchNode->IsActive())
		{
			// looking for the closet change that affected the path

			for (IT iter = first; iter != last; ++iter)
			{
				index_t changePathID = iter->GetPathID();

				if (   (  bShowAll 
					   && path.IsSameOrParentOf (changePathID))
					|| (  (iter->GetAction() != CRevisionInfoContainer::ACTION_CHANGED)
					   && path.GetBasePath().IsSameOrChildOf (changePathID)))
				{
					skipSubTree = false;

					CDictionaryBasedPath changePath = iter->GetPath();

					// construct the action member

					int actionValue = iter->GetAction();
					if (iter->HasFromPath())
						++actionValue;

					CRevisionEntry::Action action 
						= static_cast<CRevisionEntry::Action>(actionValue);

					// show modifications within the sub-tree as "modified"
					// (otherwise, deletions would terminate the path)

					if (bShowAll && (path.GetBasePath().GetIndex() < changePath.GetIndex()))
						action = CRevisionEntry::modified;

					if (   (action == CRevisionEntry::added)
						&& (searchNode->GetLastEntry() != NULL))
					{
						// we may not add paths that already exist:
						// D /trunk/OldSub
						// A /trunk/New
						// A /trunk/New/OldSub	/trunk/OldSub@r-1

						continue;
					}

					// create & init the new graph node

					CRevisionEntry* newEntry 
                        = CRevisionEntry::Create (path, revision, action, nodePool);
					newEntry->realPath = changePath;
					m_entryPtrs.push_back (newEntry);

					// link entries for the same search path

					searchNode->ChainEntries (newEntry);

					// end of path?

					if (action == CRevisionEntry::deleted)
						toRemove.push_back (searchNode);

					// we will create at most one node per path and revision

					break;
				}
				else
				{
					// store last modifying revision in search node

					if (!bShowAll && path.IsSameOrParentOf (changePathID))
					{
						searchNode->SetStartRevision (revision);
						skipSubTree = false;
					}
				}
			}
		}
		else
		{
			// can we skip the whole sub-tree?

			for (IT iter = first; iter != last; ++iter)
			{
				index_t changePathID = iter->GetPathID();

				if (   path.IsSameOrParentOf (changePathID)
					|| (  (iter->GetAction() != CRevisionInfoContainer::ACTION_CHANGED)
					   && path.GetBasePath().IsSameOrChildOf (changePathID)))
				{
					skipSubTree = false;
					break;
				}
			}
		}

		// to the next node

		searchNode = skipSubTree
				   ? searchNode->GetSkipSubTreeNext (startNode)
				   : searchNode->GetPreOrderNext (startNode);
	}
	while (searchNode != startNode);

}

void CRevisionGraph::AnalyzeChangesOnly ( revision_t revision
									    , CRevisionInfoContainer::CChangesIterator first
									    , CRevisionInfoContainer::CChangesIterator last
									    , CSearchPathTree* startNode)
{
	typedef CRevisionInfoContainer::CChangesIterator IT;

	CSearchPathTree* searchNode = startNode;
	do
	{
		// in many cases, we want only to see additions, 
		// deletions and replacements

		bool skipSubTree = true;

		// if the path is not "fully cached", there can be no changes to it

		if (searchNode->GetPath().IsFullyCachedPath())
		{
			const CDictionaryBasedPath& path = searchNode->GetPath().GetBasePath();

			// looking for the closet change that affected the path

			for (IT iter = first; iter != last; ++iter)
			{
				index_t changePathID = iter->GetPathID();

				// store last modifying revision in search node

				if (path.IsSameOrParentOf (changePathID))
				{
					if (searchNode->IsActive())
						searchNode->SetStartRevision (revision);

					skipSubTree = false;
					break;
				}
			}
		}

		// to the next node

		searchNode = skipSubTree
				   ? searchNode->GetSkipSubTreeNext (startNode)
				   : searchNode->GetPreOrderNext (startNode);
	}
	while (searchNode != startNode);
}

void CRevisionGraph::AddCopiedPaths ( revision_t revision
								    , CSearchPathTree* rootNode
								    , TSCopyIterator& lastToCopy)
{
	TSCopyIterator endToCopy = copyToRelation.end();

	// find first entry for this revision (or first beyond)

	TSCopyIterator firstToCopy = lastToCopy;
	while (   (firstToCopy != endToCopy) 
		   && ((*firstToCopy)->toRevision < revision))
		++firstToCopy;

	// find first beyond this revision

	lastToCopy = firstToCopy;
	while (   (lastToCopy != endToCopy) 
		   && ((*lastToCopy)->toRevision <= revision))
		++lastToCopy;

	// create search paths for all *relevant* paths added in this revision

	for (TSCopyIterator iter = firstToCopy; iter != lastToCopy; ++iter)
	{
		const std::vector<SCopyInfo::STarget>& targets = (*iter)->targets;
		for (size_t i = 0, count = targets.size(); i < count; ++i)
		{
			const SCopyInfo::STarget& target = targets[i];
			CSearchPathTree* node = rootNode->Insert (target.path, revision);
			node->ChainEntries (target.source);
		}
	}
}

void CRevisionGraph::FillCopyTargets ( revision_t revision
								     , CSearchPathTree* rootNode
								     , TSCopyIterator& lastFromCopy
                                     , bool exactCopy)
{
	TSCopyIterator endFromCopy = copyFromRelation.end();

	// find first entry for this revision (or first beyond)

	TSCopyIterator firstFromCopy = lastFromCopy;
	while (   (firstFromCopy != endFromCopy) 
		   && ((*firstFromCopy)->fromRevision < revision))
		++firstFromCopy;

	// find first beyond this revision

	lastFromCopy = firstFromCopy;
	while (   (lastFromCopy != endFromCopy) 
		   && ((*lastFromCopy)->fromRevision <= revision))
		++lastFromCopy;

	// create search paths for all *relevant* paths added in this revision

	for (TSCopyIterator iter = firstFromCopy; iter != lastFromCopy; ++iter)
	{
		SCopyInfo* copy = *iter;
		std::vector<SCopyInfo::STarget>& targets = copy->targets;

		// crawl the whole sub-tree for path matches

		CSearchPathTree* startNode 
			= rootNode->FindCommonParent (copy->fromPathIndex);
		if (!startNode->GetPath().IsSameOrChildOf (copy->fromPathIndex))
			continue;

		CSearchPathTree* searchNode = startNode;
		do
		{
			const CDictionaryBasedTempPath& path = searchNode->GetPath();
            assert (path.IsSameOrChildOf (copy->fromPathIndex));

			// got this path copied?

			if (searchNode->IsActive())
			{
				CDictionaryBasedPath fromPath ( path.GetDictionary()
											  , copy->fromPathIndex);

                // is there a better match in that target revision?
                // example log @r106:
                // A /trunk/F    /trunk/branches/b/F    100
                // R /trunk/F/a  /trunk/branches/b/F/a  105
                // -> don't copy from r100 but from r105

                if (IsLatestCopySource ( query->GetCache()
                                       , revision
                                       , copy->toRevision
                                       , fromPath
                                       , path))
                {
                    // o.k. this is actual a copy we have to add to the tree

                    revision_t sourceRevision = exactCopy
											  ? revision
											  : searchNode->GetStartRevision();

					CRevisionEntry*	entry = searchNode->GetLastEntry();
				    if ((entry == NULL) || (entry->revision < sourceRevision))
				    {
					    // the copy source graph node has yet to be created

                        entry = CRevisionEntry::Create ( path
											           , revision
											           , CRevisionEntry::source
                                                       , nodePool);
					    entry->realPath = fromPath;

					    m_entryPtrs.push_back (entry);

					    searchNode->ChainEntries (entry);
				    }

                    // add & schedule the new search path

				    SCopyInfo::STarget target 
					    ( entry
					    , path.ReplaceParent ( fromPath
									         , CDictionaryBasedPath ( path.GetDictionary()
															        , copy->toPathIndex)));

				    targets.push_back (target);
			    }
            }

			// select next node

			searchNode = searchNode->GetPreOrderNext (startNode);
		}
		while (searchNode != startNode);
	}
}

bool CRevisionGraph::IsLatestCopySource ( const CCachedLogInfo* cache
                                        , revision_t fromRevision
                                        , revision_t toRevision
                                        , const CDictionaryBasedPath& fromPath
                                        , const CDictionaryBasedTempPath& currentPath)
{
    // try to find a "later" / "closer" copy source

    // example log @r106 (toRevision):
    // A /trunk/F    /trunk/branches/b/F    100
    // R /trunk/F/a  /trunk/branches/b/F/a  105
    // -> return r105

    const CRevisionInfoContainer& logInfo = cache->GetLogInfo();
    index_t index = cache->GetRevisions()[toRevision];

    // search it

    for ( CRevisionInfoContainer::CChangesIterator 
          iter = logInfo.GetChangesBegin (index)
        , end = logInfo.GetChangesEnd (index)
        ; iter != end
        ; ++iter)
    {
        // is this a copy of the current path?

        if (   iter->HasFromPath() 
            && currentPath.IsSameOrChildOf (iter->GetFromPathID()))
        {
            // a later change?

            if (iter->GetFromRevision() > fromRevision)
                return false;

            // a closer sub-path?

            if (iter->GetFromPathID() > fromPath.GetIndex())
                return false;
        }
    }

    // (fromRevision, fromGraph) is the best match

    return true;
}

void CRevisionGraph::AddMissingHeads (CSearchPathTree* rootNode)
{
	const CCachedLogInfo* cache = query->GetCache();
	const CRevisionIndex& revisions = cache->GetRevisions();
	const CRevisionInfoContainer& revisionInfo = cache->GetLogInfo();

    // collect search paths that have we know the head of already in this container
    // (delay potential node deletion until we finished tree traversal)

    std::vector<CSearchPathTree*> toRemove;

	// collect all nodes that don't have a path used in any change

	CSearchPathTree* searchNode = rootNode;
    for ( searchNode = rootNode
        ; searchNode != NULL
        ; searchNode = searchNode->GetPreOrderNext())
	{
        if (   searchNode->IsActive() 
            && !searchNode->GetPath().IsFullyCachedPath())
        {
            // path is not known in cache -> no change for this path

            toRemove.push_back (searchNode);
        }
    }

    // collect nodes to draw ... revision by revision

	for ( revision_t revision = m_lHeadRevision
        ; (revision > 0) && !rootNode->IsEmpty()
        ; --revision)
	{
		index_t index = revisions[revision];
		if (index == NO_INDEX)
			continue;

	    // remove deleted search paths

	    for (size_t i = 0, count = toRemove.size(); i < count; ++i)
		    toRemove[i]->Remove();

        toRemove.clear();

		// we are looking for search paths that (may) overlap 
		// with the revisions' changes

		CDictionaryBasedPath basePath = revisionInfo.GetRootPath (index);
		if (!basePath.IsValid())
			continue;	// empty revision

		// pre-order search-tree traversal

		CSearchPathTree* searchNode = rootNode;
		while (searchNode != NULL)
		{
			bool subTreeTouched 
				= searchNode->GetPath().IsSameOrParentOf (basePath);
            bool parentTreeTouched
                = searchNode->GetPath().IsSameOrChildOf (basePath);

            // inspect sub-tree only if there is an overlap

            if (subTreeTouched || parentTreeTouched)
            {
                // if this path is active, 
                // check whether this is the head revision
                // and add a node, if not already present
                // and schedule it for removal from the search tree

				AnalyzeHeadRevision ( revision
    								, revisionInfo.GetChangesBegin (index)
	    							, revisionInfo.GetChangesEnd (index)
		    						, searchNode
			    					, toRemove);

                // continue on children, if there are any

                if (searchNode->GetFirstChild())
                {
                    searchNode = searchNode->GetFirstChild();
                    continue;
                }
            }

			// continue with right next

            searchNode = searchNode->GetSkipSubTreeNext();
		}
	}
}

void CRevisionGraph::MarkHeads (CSearchPathTree* rootNode)
{
	// scan all "latest" nodes 
    // (they must be either HEADs or special nodes)

	CSearchPathTree* searchNode = rootNode;
    for ( searchNode = rootNode
        ; searchNode != NULL
        ; searchNode = searchNode->GetPreOrderNext())
	{
        if (searchNode->IsActive())
        {
            CRevisionEntry* entry = searchNode->GetLastEntry();
            if (   (entry->action == CRevisionEntry::nothing)
                || (entry->action == CRevisionEntry::modified))
            {
                // be more specific for head revisions that are not
                // "special" (added / deleted / ...) otherwise

                entry->action = CRevisionEntry::lastcommit;
            }
        }
    }
}

void CRevisionGraph::AnalyzeHeadRevision ( revision_t revision
    									 , CRevisionInfoContainer::CChangesIterator first
	    								 , CRevisionInfoContainer::CChangesIterator last
		    							 , CSearchPathTree* searchNode
				    					 , std::vector<CSearchPathTree*>& toRemove)
{
    // not an active search path anymore?

    if (!searchNode->IsActive())
        return;

    // head revision already known (i.e. node already exists)?

    if (!searchNode->YetToCover (revision))
    {
        toRemove.push_back (searchNode);
        return;
    }

    // detailed inspection of all changes until we find a match

    const CDictionaryBasedTempPath& tempPath = searchNode->GetPath();
    const CDictionaryBasedPath& path = tempPath.GetBasePath();
	for ( CRevisionInfoContainer::CChangesIterator iter = first
		; iter != last
		; ++iter)
	{
        if (path.IsSameOrParentOf (iter->GetPathID()))
		{
			// create & init the new graph node

			CRevisionEntry* newEntry 
                = CRevisionEntry::Create ( tempPath
                                         , revision
                                         , CRevisionEntry::lastcommit
                                         , nodePool);
            newEntry->realPath = iter->GetPath();
			m_entryPtrs.push_back (newEntry);

			// link entries for the same search path

			searchNode->ChainEntries (newEntry);

			// head found

			toRemove.push_back (searchNode);

			// we will create at most one node per path and revision

			break;
		}
	}
}

void CRevisionGraph::FindReplacements()
{
	// say "renamed" for "Deleted"/"Added" entries

	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
	{
		CRevisionEntry * entry = m_entryPtrs[i];
		CRevisionEntry * next = entry->next;

		if ((next != NULL) && (next->action == CRevisionEntry::deleted))
		{
			// this line will be deleted. 
			// will it be continued exactly once under a different name?

            CRevisionEntry * renameTarget = NULL;
            size_t renameIndex = 0;

			for (size_t k = entry->copyTargets.size(); k > 0; --k)
			{
				CRevisionEntry * copy = entry->copyTargets[k-1];
				assert (copy->action == CRevisionEntry::addedwithhistory);

				if (copy->revision == next->revision)
				{
					// that actually looks like a rename

                    if (renameTarget != NULL)
                    {
                        // there is more than one copy target 
                        // -> display all individual deletion and additions 

                        renameTarget = NULL;
                        break;
                    }
                    else
                    {
                        // remember the (potential) rename target

                        renameTarget = copy;
                        renameIndex = k-1;
                    }
                }
            }

            // did we find a unambiguous rename target?

            if (renameTarget != NULL)
            {
                // optimize graph

				renameTarget->action = CRevisionEntry::renamed;

				// make it part of this line (not a branch)

				entry->next = renameTarget;
                renameTarget->prev = entry;

                assert (renameTarget->copySource != NULL);
                renameTarget->copySource = NULL;

				entry->copyTargets[renameIndex] = *entry->copyTargets.rbegin();
                entry->copyTargets.pop_back();

				// mark the old "deleted" entry for removal

				next->action = CRevisionEntry::nothing;
			}
		}
	}
}

// classify nodes on by one

void CRevisionGraph::ForwardClassification()
{
	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
    {
        CRevisionEntry* entry = m_entryPtrs[i];

        entry->classification = (*pathClassification)[entry->path];
        switch (entry->action)
        {
        case CRevisionEntry::deleted:
            entry->classification |= CPathClassificator::SUBTREE_DELETED;
            break;

        case CRevisionEntry::modified:
        case CRevisionEntry::source:
        case CRevisionEntry::lastcommit:
            entry->classification |= CPathClassificator::IS_MODIFIED;
        }
    }
}

// propagate classification back along copy history

void CRevisionGraph::BackwardClassification (const SOptions& options)
{
	for (size_t i = m_entryPtrs.size(); i > 0; --i)
    {
        CRevisionEntry* entry = m_entryPtrs[i-1];
        DWORD classification = entry->classification;

        // copy info along the branch / tag / trunk line

        if (entry->next != NULL)
        {
            DWORD mask =   CPathClassificator::SUBTREE_DELETED
                         + CPathClassificator::IS_MODIFIED;

            classification |=  entry->next->classification & mask;
        }

        // copy classification along copy history

        const std::vector<CRevisionEntry*>& targets = entry->copyTargets;
        for (size_t k = 0, copyCount = targets.size(); k  < copyCount; ++k)
        {
            DWORD targetClassification = targets[k]->classification;

            // deletion info (is there at least one surviving copy?)

            bool subTreeDeleted 
                =    (targetClassification & CPathClassificator::SUBTREE_DELETED)
                  == CPathClassificator::SUBTREE_DELETED;

            if (!subTreeDeleted)
                classification &= ~CPathClassificator::ALL_COPIES_DELETED;

            // transitive and immediate copy and modification info
            // (don't propagate copy target info if it will be removed anyway)

            if (!subTreeDeleted || !options.removeDeletedOnes)
            {
                const DWORD transitiveMask =   CPathClassificator::COPIES_TO_MASK
                                             + CPathClassificator::IS_MODIFIED;

                classification |=   (targetClassification & transitiveMask)
                                  | ((targetClassification & CPathClassificator::IS_MASK) * 0x10);
            }
        }

        // write back

        entry->classification = classification;
    }
}

void CRevisionGraph::RemoveDeletedOnes()
{
    // mark all deleted sub-trees for removal
    // and disconnect them from the remaining graph

	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
    {
        CRevisionEntry* entry = m_entryPtrs[i];

        if (   (entry->classification & CPathClassificator::SUBTREE_DELETED)
            == CPathClassificator::SUBTREE_DELETED)
        {
            // mark this node for deletion

            entry->action = CRevisionEntry::nothing;

            // node predecessor

            CRevisionEntry* prev = entry->prev != NULL
                                 ? entry->prev 
                                 : entry->copySource != NULL
                                    ? entry->copySource
                                    : NULL;

            // do nothing if that has been deleted as well
            // (we are within a deleted sub-tree in that case)

            if ((prev != NULL) && (prev->action != CRevisionEntry::nothing))
            {
                // de-link root of deleted sub-tree

                if (entry->prev == NULL)
                {
                    std::vector<CRevisionEntry*>& targets = prev->copyTargets;
                    targets.erase (std::find ( targets.begin()
                                             , targets.end()
                                             , entry));

                    entry->copySource = NULL;
                }
                else
                {
                    assert (entry->copySource == NULL);
                    prev->next = NULL;
                    entry->prev = NULL;
                }

                // remove copy source, 
                // if it is a simple copy source and is no longer needed

                if (   (prev->action == CRevisionEntry::source)
                    && (prev->copyTargets.empty()))
                {
                    // de-link and mark for removal

                    if (prev->prev != NULL)
                        prev->prev->next = prev->next;
                    if (prev->next != NULL)
                        prev->next->prev = prev->prev;

                    prev->action = CRevisionEntry::nothing;
                }
            }
        }
    }
}

void CRevisionGraph::FoldTags ( CRevisionEntry * collectorNode
                              , CRevisionEntry * entry
                              , unsigned depth)
{
    bool firstRun = true;

    for (; entry != NULL; entry = entry->next)
    {
        // don't remove the collector node
        // and don't show deletions as new tags

        if (collectorNode != entry)
        {
            if (   (entry->action == CRevisionEntry::addedwithhistory)
                || (entry->action == CRevisionEntry::renamed))
            {
                // add tag to collector

                CRevisionEntry::SFoldedTag tag 
                    ( entry->path
                    , !firstRun
                    , (entry->classification & CPathClassificator::IS_DELETED) != 0
                    , depth);

                collectorNode->tags.push_back (tag);
            }

            // schedule for deletion
                
            entry->action = CRevisionEntry::nothing;

            // remove from collector node

            if (depth == 0)
            {
                if (entry->prev == NULL)
                {
                    if (entry->copySource != NULL)
                    {
                        typedef std::vector<CRevisionEntry*>::iterator TI;

                        std::vector<CRevisionEntry*>& targets 
                            = collectorNode->copyTargets;

                        TI begin = targets.begin();
                        TI end = targets.end();
                        targets.erase (std::find (begin, end, entry));
                    }
                }
                else
                {
                    entry->prev->next = NULL;
                }
            }
        }

        // add all copies of that tag

        const std::vector<CRevisionEntry*>& targets = entry->copyTargets;
        for (size_t i = 0, count = targets.size(); i  < count; ++i)
            FoldTags (collectorNode, targets[i], depth+1);

        // all others on this line are aliases

        firstRun = false;
    }
}

void CRevisionGraph::FoldTags()
{
    // look for copy targets that have no further nodes and contain "tags"

    DWORD nonTagOpMask =   CPathClassificator::IS_MASK 
                         - CPathClassificator::IS_TAG
                         + CPathClassificator::COPIES_TO_MASK 
                         - CPathClassificator::COPIES_TO_TAG
                         + CPathClassificator::IS_MODIFIED;

	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
	{
		CRevisionEntry * entry = m_entryPtrs[i];

        // is this only a tag?
        // (possibly copied, renamed or deleted but unmodified)

        if (   (entry->action != CRevisionEntry::nothing)
            && ((entry->classification & nonTagOpMask) == 0))
        {
            CRevisionEntry * foldInto 
                = entry->prev != NULL
                ? entry->prev
                : entry->copySource != NULL
                    ? entry->copySource
                    : entry;

            FoldTags (foldInto, entry, 0);
        }
	}
}

void CRevisionGraph::ApplyFilter()
{
	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
	{
		CRevisionEntry * entry = m_entryPtrs[i];

		bool bRemove = false;
		if (m_filterpaths.size() > 0)
		{
			for (std::set<std::string>::iterator fi = m_filterpaths.begin(); fi != m_filterpaths.end(); ++fi)
			{
				if (entry->realPath.GetPath().find(fi->c_str()) != std::string::npos)
				{
					bRemove = true;
					break;
				}
			}
		}
		if (((svn_revnum_t)entry->revision < m_FilterMinRev) ||
			((svn_revnum_t)entry->revision > m_FilterMaxRev) ||
			(bRemove))
		{
			CRevisionEntry* source = entry->copySource;
			if (source != NULL)
			{
				for ( std::vector<CRevisionEntry*>::iterator it = source->copyTargets.begin()
					; it != source->copyTargets.end()
					; ++it)
				{
					if (*it == entry)
					{
						source->copyTargets.erase(it);
						break;
					}
				}
			}
			if (entry->prev)
			{
				entry->prev->next = entry->next;
			}
            if (entry->next)
            {
				entry->next->prev = entry->prev;
            }
			entry->action = CRevisionEntry::nothing;
		}
	}
}

void CRevisionGraph::Compact()
{
	std::vector<CRevisionEntry*>::iterator target = m_entryPtrs.begin();
	for ( std::vector<CRevisionEntry*>::iterator source = target
		, end = m_entryPtrs.end()
		; source != end
		; ++source)
	{
		if ((*source)->action == CRevisionEntry::nothing)
		{
			(*source)->Destroy (nodePool);
		}
		else
		{
			*target = *source;
			++target;
		}
	}

	m_entryPtrs.erase (target, m_entryPtrs.end());
}

void CRevisionGraph::Optimize (const SOptions& options)
{
	// say "renamed" for "Deleted"/"Added" entries

    FindReplacements();

    // classify all nodes (needs to fully passes):
    // classify nodes on by one

    ForwardClassification();

    // propagate classification back along copy history

    BackwardClassification (options);

    // remove all paths that have been deleted

    if (options.removeDeletedOnes)
        RemoveDeletedOnes();

	// apply the custom filter

	ApplyFilter();
	
	// fold tags if requested

    if (options.foldTags)
        FoldTags();

    // compact

    Compact();
}

// assign columns to branches recursively from the left to the right 
// (one branch level per recursive step)

void CRevisionGraph::AssignColumns ( CRevisionEntry* start
								   , std::vector<int>& columnByRow
                                   , int column
								   , const SOptions& options)
{
	// find the first row that will be occupied by this branch

	int startRow = start->row;
	if (options.reduceCrossLines && (startRow > 0))
	{
		// in most cases of lines cross boxes, the reason is
		// one branch ending on one line with the next one
		// starting at the next (using the same column)
		// -> just mark one additional row as "used" by this
		//    branch. So, there will be at least one "space"
		//    row between branches in the same column.

		--startRow;
	}

	// find largest column for the chain starting at "start"
    // skip split branch sections

	int lastRow = startRow;
	for (CRevisionEntry* entry = start; entry != NULL; entry = entry->next)
    {
        for (int row = lastRow; row <= entry->row; ++row)
	        column = max (column, columnByRow[row]+1);

		lastRow = entry->row;
    }

	// assign that column & collect branches

	std::vector<CRevisionEntry*> branches;
	for (CRevisionEntry* entry = start; entry != NULL; entry = entry->next)
	{
		entry->column = column;
		if (!entry->copyTargets.empty())
			branches.push_back (entry);
	}

	// block the column for the whole chain except for split branch sections

	lastRow = startRow;
	for (CRevisionEntry* entry = start; entry != NULL; entry = entry->next)
    {
        for (int row = lastRow; row <= entry->row; ++row)
    		columnByRow[row] = column;

		lastRow = entry->row;
    }

	// follow the branches

	for ( std::vector<CRevisionEntry*>::reverse_iterator iter = branches.rbegin()
		, end = branches.rend()
		; iter != end
		; ++iter)
	{
		const std::vector<CRevisionEntry*>& targets = (*iter)->copyTargets;
		for (size_t i = 0, count = targets.size(); i < count; ++i)
			AssignColumns (targets[i], columnByRow, column+1, options);
	}
}

int CRevisionGraph::AssignOneRowPerRevision()
{
	int row = 0;
	revision_t lastRevision = 0;
	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
	{
		CRevisionEntry * entry = m_entryPtrs[i];
		if (entry->revision > lastRevision)
		{
			lastRevision = entry->revision;
			++row;
		}
		
		entry->row = row;
	}

	// return first unused rows

	return row+1;
}

int CRevisionGraph::AssignOneRowPerBranchNode (CRevisionEntry* start, int row)
{
	int maxRow = row;
	for (CRevisionEntry* node = start; node != NULL; node = node->next)
	{
		const std::vector<CRevisionEntry*>& targets = node->copyTargets;
		if (targets.empty())
		{
			node->row = row;
			++row;
			maxRow = max (maxRow, row);
		}
		else
		{
			row = maxRow;
			node->row = row;
			++row;

			for (size_t i = 0, count = targets.size(); i < count; ++i)
				maxRow = max (maxRow, AssignOneRowPerBranchNode (targets[i], row));
		}
	}

	// return first unused row

	return maxRow;
}

void CRevisionGraph::ReverseRowOrder (int maxRow)
{
	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
    {
		CRevisionEntry * entry = m_entryPtrs[i];
        entry->row = maxRow - entry->row;
    }
}

void CRevisionGraph::AssignCoordinates (const SOptions& options)
{
    // pathological but not impossible:

    if (m_entryPtrs.empty())
        return;

	// assign rows

    int row = options.groupBranches
			? AssignOneRowPerBranchNode (m_entryPtrs[0], 1)
			: AssignOneRowPerRevision();

	// the highest used column per revision

	std::vector<int> columnByRow;
	columnByRow.insert (columnByRow.begin(), row+1, 0);

	AssignColumns (m_entryPtrs[0], columnByRow, 1, options);

    // invert order (show newest rev in first row)

    if (!options.oldestAtTop)
        ReverseRowOrder (row);
}

inline bool AscendingColumRow ( const CRevisionEntry* lhs
							  , const CRevisionEntry* rhs)
{
	return (lhs->column < rhs->column)
		|| (   (lhs->column == rhs->column)
		    && (lhs->row < rhs->row));
}

void CRevisionGraph::Cleanup()
{
	// sort targets by level and revision

	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
	{
		CRevisionEntry * entry = m_entryPtrs[i];
		std::vector<CRevisionEntry*>& targets = entry->copyTargets;
		if (targets.size() > 1)
			sort (targets.begin(), targets.end(), &AscendingColumRow);
	}
}

CString CRevisionGraph::GetLastErrorMessage()
{
	return SVN::GetErrorString(Err);
}

CString CRevisionGraph::GetReposRoot() 
{
    return CUnicodeUtils::GetUnicode (CPathUtils::PathUnescape (m_sRepoRoot));
}

