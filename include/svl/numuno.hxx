/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */
#ifndef INCLUDED_SVL_NUMUNO_HXX
#define INCLUDED_SVL_NUMUNO_HXX

#include <svl/svldllapi.h>
#include <com/sun/star/util/XNumberFormatsSupplier.hpp>
#include <com/sun/star/lang/XUnoTunnel.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <cppuhelper/implbase2.hxx>

class SvNumberFormatter;
class SvNumFmtSuppl_Impl;

namespace comphelper
{
    class SharedMutex;
}



//  SvNumberFormatterServiceObj must be registered as service somewhere

com::sun::star::uno::Reference<com::sun::star::uno::XInterface> SAL_CALL
    SvNumberFormatterServiceObj_NewInstance(
        const com::sun::star::uno::Reference<
            com::sun::star::lang::XMultiServiceFactory>& rSMgr );



//  SvNumberFormatsSupplierObj: aggregate to document,
//  construct with SvNumberFormatter

class SVL_DLLPUBLIC SvNumberFormatsSupplierObj : public cppu::WeakAggImplHelper2<
                                    com::sun::star::util::XNumberFormatsSupplier,
                                    com::sun::star::lang::XUnoTunnel>
{
private:
    SvNumFmtSuppl_Impl* pImpl;

public:
                                SvNumberFormatsSupplierObj();
                                SvNumberFormatsSupplierObj(SvNumberFormatter* pForm);
    virtual                     ~SvNumberFormatsSupplierObj();

    void                        SetNumberFormatter(SvNumberFormatter* pNew);
    SvNumberFormatter*          GetNumberFormatter() const;

                                // overload to adapt attributes in the document
    virtual void                NumberFormatDeleted(sal_uInt32 nKey);
                                // overload to possibly format something anew
    virtual void                SettingsChanged();

                                // XNumberFormatsSupplier
    virtual ::com::sun::star::uno::Reference< ::com::sun::star::beans::XPropertySet > SAL_CALL
                                getNumberFormatSettings()
                                    throw(::com::sun::star::uno::RuntimeException, std::exception) SAL_OVERRIDE;
    virtual ::com::sun::star::uno::Reference< ::com::sun::star::util::XNumberFormats > SAL_CALL
                                getNumberFormats()
                                    throw(::com::sun::star::uno::RuntimeException, std::exception) SAL_OVERRIDE;

                                // XUnoTunnel
    virtual sal_Int64 SAL_CALL  getSomething( const ::com::sun::star::uno::Sequence<
                                    sal_Int8 >& aIdentifier )
                                        throw(::com::sun::star::uno::RuntimeException, std::exception) SAL_OVERRIDE;

    static const com::sun::star::uno::Sequence<sal_Int8>& getUnoTunnelId();
    static SvNumberFormatsSupplierObj* getImplementation( const com::sun::star::uno::Reference<
                                    com::sun::star::util::XNumberFormatsSupplier> xObj );

    ::comphelper::SharedMutex&  getSharedMutex() const;
};

#endif // INCLUDED_SVL_NUMUNO_HXX


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
