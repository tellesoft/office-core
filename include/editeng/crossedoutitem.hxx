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
#ifndef INCLUDED_EDITENG_CROSSEDOUTITEM_HXX
#define INCLUDED_EDITENG_CROSSEDOUTITEM_HXX

#include <tools/fontenum.hxx>
#include <svl/eitem.hxx>
#include <editeng/editengdllapi.h>

// class SvxCrossedOutItem -----------------------------------------------

/*  [Description]
    This item describes, whether and how it is striked out.
*/

class EDITENG_DLLPUBLIC SvxCrossedOutItem final : public SfxEnumItem<FontStrikeout>
{
protected:
    virtual ItemInstanceManager* getItemInstanceManager() const override;

public:
    static SfxPoolItem* CreateDefault();

    DECLARE_ITEM_TYPE_FUNCTION(SvxCrossedOutItem)
    SvxCrossedOutItem( const FontStrikeout eSt /*= STRIKEOUT_NONE*/,
                       const sal_uInt16 nId  );

    // "pure virtual Methods" from SfxPoolItem
    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText, const IntlWrapper& ) const override;

    virtual SvxCrossedOutItem* Clone( SfxItemPool *pPool = nullptr ) const override;
    static OUString         GetValueTextByPos( sal_uInt16 nPos );
    virtual bool            QueryValue( css::uno::Any& rVal, sal_uInt8 nMemberId = 0 ) const override;
    virtual bool            PutValue( const css::uno::Any& rVal, sal_uInt8 nMemberId ) override;

    using SfxEnumItem::SetValue;

    virtual bool            HasBoolValue() const override;
    virtual bool            GetBoolValue() const override;
    virtual void            SetBoolValue( bool bVal ) override;

    SvxCrossedOutItem(SvxCrossedOutItem const &) = default; // SfxPoolItem copy function dichotomy

    // enum cast
    FontStrikeout           GetStrikeout() const
                                { return GetValue(); }
};

#endif // INCLUDED_EDITENG_CROSSEDOUTITEM_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
