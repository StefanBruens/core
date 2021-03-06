/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_SC_GROUPAREALISTENER_HXX
#define INCLUDED_SC_GROUPAREALISTENER_HXX

#include <address.hxx>
#include <calcmacros.hxx>

#include <svl/listener.hxx>

class ScFormulaCell;

namespace sc {

class BulkDataHint;

class FormulaGroupAreaListener : public SvtListener
{
    ScRange maRange;
    ScFormulaCell** mppTopCell;
    SCROW mnGroupLen;
    bool mbStartFixed;
    bool mbEndFixed;

    FormulaGroupAreaListener(); // disabled

public:

    FormulaGroupAreaListener(
        const ScRange& rRange, ScFormulaCell** ppTopCell, SCROW nGroupLen, bool bStartFixed, bool bEndFixed );

    ScRange getListeningRange() const;

    virtual void Notify( const SfxHint& rHint ) SAL_OVERRIDE;
    virtual void Query( QueryBase& rQuery ) const SAL_OVERRIDE;

    /**
     * Given the row span of changed cells within a single column, collect all
     * formula cells that need to be notified of the change.
     *
     * @param nTab sheet position of the changed cell span.
     * @param nCol column position of the changed cell span.
     * @param nRow1 top row position of the changed cell span.
     * @param nRow2 bottom row position of the changed cell span.
     * @param rCells all formula cells that need to be notified are put into
     *               this container.
     */
    void collectFormulaCells( SCTAB nTab, SCCOL nCol, SCROW nRow1, SCROW nRow2, std::vector<ScFormulaCell*>& rCells ) const;
    void collectFormulaCells( SCROW nRow1, SCROW nRow2, std::vector<ScFormulaCell*>& rCells ) const;

    ScAddress getTopCellPos() const;
    const ScRange& getRange() const;
    SCROW getGroupLength() const;
    bool isStartFixed() const;
    bool isEndFixed() const;

private:
    void notifyCellChange( const SfxHint& rHint, const ScAddress& rPos );
    void notifyBulkChange( const BulkDataHint& rHint );
};

}

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
