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

#include <TextObjectBar.hxx>

#include <svx/svxids.hrc>

#include <com/sun/star/linguistic2/XThesaurus.hpp>

#include <editeng/eeitem.hxx>
#include <editeng/udlnitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/lspcitem.hxx>
#include <editeng/adjustitem.hxx>
#include <editeng/cmapitem.hxx>
#include <editeng/editview.hxx>
#include <editeng/outliner.hxx>
#include <editeng/unolingu.hxx>
#include <editeng/kernitem.hxx>
#include <editeng/editund2.hxx>
#include <svl/whiter.hxx>
#include <svl/itempool.hxx>
#include <svl/stritem.hxx>
#include <svl/style.hxx>
#include <svl/languageoptions.hxx>
#include <svl/cjkoptions.hxx>
#include <svl/ctloptions.hxx>
#include <sfx2/tplpitem.hxx>
#include <editeng/escapementitem.hxx>
#include <svx/svdoutl.hxx>
#include <editeng/scriptsetitem.hxx>
#include <editeng/writingmodeitem.hxx>
#include <editeng/frmdiritem.hxx>
#include <editeng/fhgtitem.hxx>
#include <o3tl/temporary.hxx>

#include <sfx2/objface.hxx>

#include <drawdoc.hxx>
#include <drawview.hxx>
#include <DrawDocShell.hxx>
#include <DrawViewShell.hxx>
#include <OutlineViewShell.hxx>
#include <NotesPanelViewShell.hxx>
#include <Window.hxx>
#include <OutlineView.hxx>
#include <Outliner.hxx>

#define ShellClass_TextObjectBar
using namespace sd;
#include <sdslots.hxx>

using namespace ::com::sun::star;

namespace sd {

/**
 * Declare default interface (Slotmap must not be empty, therefore enter
 * something that (hopefully) never occurs.
 */
SFX_IMPL_INTERFACE(TextObjectBar, SfxShell)

void TextObjectBar::InitInterface_Impl()
{
}


TextObjectBar::TextObjectBar (
    ViewShell& rSdViewSh,
    SfxItemPool& rItemPool,
    ::sd::View* pSdView )
    : SfxShell(rSdViewSh.GetViewShell()),
      mrViewShell( rSdViewSh ),
      mpView( pSdView )
{
    SetPool(&rItemPool);

    if( mpView )
    {
        OutlineView* pOutlinerView = dynamic_cast< OutlineView* >( mpView );
        if( pOutlinerView )
        {
            SetUndoManager(&pOutlinerView->GetOutliner().GetUndoManager());
        }
        else
        {
            DrawDocShell* pDocShell = mpView->GetDoc().GetDocSh();
            if( pDocShell )
            {
                SetUndoManager(pDocShell->GetUndoManager());
                DrawViewShell* pDrawViewShell = dynamic_cast< DrawViewShell* >( &rSdViewSh );
                if ( pDrawViewShell )
                    SetRepeatTarget(pSdView);
            }
        }
    }

    SetName( u"TextObjectBar"_ustr);

    // SetHelpId( SD_IF_SDDRAWTEXTOBJECTBAR );
}

TextObjectBar::~TextObjectBar()
{
    SetRepeatTarget(nullptr);
}

void TextObjectBar::GetCharState( SfxItemSet& rSet )
{
    GetCharStateImpl(mrViewShell, mpView, rSet);
}

void TextObjectBar::GetCharStateImpl(ViewShell& rViewShell, const ::sd::View* pView, SfxItemSet& rSet)
{
    SfxItemSet  aCharAttrSet( pView->GetDoc().GetPool() );
    pView->GetAttributes( aCharAttrSet );

    SfxItemSetFixed<EE_ITEMS_START,EE_ITEMS_END> aNewAttr( rViewShell.GetPool() );

    aNewAttr.Put(aCharAttrSet, false);
    rSet.Put(aNewAttr, false);

    SvxKerningItem aKern = aCharAttrSet.Get( EE_CHAR_KERNING );
    //aKern.SetWhich(SID_ATTR_CHAR_KERNING);
    rSet.Put(aKern);

    SfxItemState eState = aCharAttrSet.GetItemState( EE_CHAR_KERNING );
    if ( eState == SfxItemState::INVALID )
    {
        rSet.InvalidateItem(EE_CHAR_KERNING);
    }
}

void TextObjectBar::GetAttrState( SfxItemSet& rSet )
{
    GetAttrStateImpl(mrViewShell, mpView, rSet, this);
}

/**
 * Status of attribute items.
 */
void TextObjectBar::GetAttrStateImpl(ViewShell& rViewShell, ::sd::View* pView, SfxItemSet& rSet, SfxShell* pTextObjectBar)
{
    SfxWhichIter        aIter( rSet );
    sal_uInt16              nWhich = aIter.FirstWhich();
    SfxItemSet          aAttrSet( pView->GetDoc().GetPool() );
    bool            bDisableParagraphTextDirection = !SvtCTLOptions::IsCTLFontEnabled();
    bool            bDisableVerticalText = !SvtCJKOptions::IsVerticalTextEnabled();

    pView->GetAttributes( aAttrSet );

    while ( nWhich )
    {
        sal_uInt16 nSlotId = SfxItemPool::IsWhich(nWhich)
            ? pView->GetDoc().GetPool().GetSlotId(nWhich)
            : nWhich;

        switch ( nSlotId )
        {
            case SID_ATTR_CHAR_FONT:
            case SID_ATTR_CHAR_FONTHEIGHT:
            case SID_ATTR_CHAR_WEIGHT:
            case SID_ATTR_CHAR_POSTURE:
            case SID_ATTR_CHAR_SHADOWED:
            case SID_ATTR_CHAR_STRIKEOUT:
            case SID_ATTR_CHAR_CASEMAP:
            {
                double stretchY = 1.0;
                SvxScriptSetItem aSetItem( nSlotId, pView->GetDoc().GetPool() );
                aSetItem.GetItemSet().Put( aAttrSet, false );

                SvtScriptType nScriptType = pView->GetScriptType();

                if( (nSlotId == SID_ATTR_CHAR_FONT) || (nSlotId == SID_ATTR_CHAR_FONTHEIGHT) )
                {
                    // input language should be preferred over
                    // current cursor position to detect script type
                    OutlinerView* pOLV = pView->GetTextEditOutlinerView();
                    SdrOutliner *pOutliner = pView->GetTextEditOutliner();

                    if (OutlineView* pOView = dynamic_cast<OutlineView*>(pView))
                        pOLV = pOView->GetViewByWindow(rViewShell.GetActiveWindow());

                    if (pOutliner)
                        stretchY = pOutliner->getScalingParameters().fFontY;

                    if(pOLV && !pOLV->GetSelection().HasRange())
                    {
                        if (rViewShell.GetViewShell() && rViewShell.GetViewShell()->GetWindow())
                        {
                            LanguageType nInputLang = rViewShell.GetViewShell()->GetWindow()->GetInputLanguage();
                            if(nInputLang != LANGUAGE_DONTKNOW && nInputLang != LANGUAGE_SYSTEM)
                                nScriptType = SvtLanguageOptions::GetScriptTypeOfLanguage( nInputLang );
                        }
                    }
                }

                const SfxPoolItem* pI = aSetItem.GetItemOfScript( nScriptType );
                if( pI )
                {
                    if( nSlotId == SID_ATTR_CHAR_FONTHEIGHT )
                    {
                        SvxFontHeightItem aFontItem = dynamic_cast<const SvxFontHeightItem&>(*pI);
                        aFontItem.SetHeight(aFontItem.GetHeight() * stretchY, 100, aFontItem.GetPropUnit());
                        aFontItem.SetWhich(nWhich);
                        aAttrSet.Put( aFontItem );
                    }
                    else
                    {
                        aAttrSet.Put( pI->CloneSetWhich(nWhich) );
                    }
                }
                else
                {
                    aAttrSet.InvalidateItem( nWhich );
                }
            }
            break;

            case SID_STYLE_APPLY:
            case SID_STYLE_FAMILY2:
            {
                SfxStyleSheet* pStyleSheet = pView->GetStyleSheetFromMarked();
                if( pStyleSheet )
                    rSet.Put( SfxTemplateItem( nWhich, pStyleSheet->GetName() ) );
                else
                {
                    rSet.Put( SfxTemplateItem( nWhich, OUString() ) );
                }
            }
            break;

            case SID_OUTLINE_LEFT:
            case SID_OUTLINE_RIGHT:
            case SID_OUTLINE_UP:
            case SID_OUTLINE_DOWN:
            {
                bool bDisableLeft     = true;
                bool bDisableRight    = true;
                bool bDisableUp       = true;
                bool bDisableDown     = true;

                //fdo#78151 it doesn't make sense to promote or demote outline levels in master view.
                const DrawViewShell* pDrawViewShell = dynamic_cast< const DrawViewShell* >(&rViewShell);
                const bool bInMasterView = pDrawViewShell && pDrawViewShell->GetEditMode() == EditMode::MasterPage;

                if (!bInMasterView)
                {
                    OutlinerView* pOLV = pView->GetTextEditOutlinerView();

                    if (OutlineView* pOView = dynamic_cast<OutlineView*>(pView))
                        pOLV = pOView->GetViewByWindow(rViewShell.GetActiveWindow());

                    bool bOutlineViewSh = dynamic_cast< const OutlineViewShell *>( &rViewShell ) !=  nullptr;

                    if (pOLV)
                    {
                        // Outliner at outline-mode
                        ::Outliner& rOutl = pOLV->GetOutliner();

                        std::vector<Paragraph*> aSelList;
                        pOLV->CreateSelectionList(aSelList);
                        Paragraph* pPara = aSelList.empty() ? nullptr : *(aSelList.begin());

                        // find out if we are an OutlineView
                        bool bIsOutlineView(OutlinerMode::OutlineView == pOLV->GetOutliner().GetOutlinerMode());

                        // This is ONLY for OutlineViews
                        if(bIsOutlineView)
                        {
                            // allow move up if position is 2 or greater OR it
                            // is a title object (and thus depth==1)
                            if(rOutl.GetAbsPos(pPara) > 1 || ( ::Outliner::HasParaFlag(pPara,ParaFlag::ISPAGE) && rOutl.GetAbsPos(pPara) > 0 ) )
                            {
                                // not at top
                                bDisableUp = false;
                            }
                        }
                        else
                        {
                            // old behaviour for OutlinerMode::OutlineObject
                            if(rOutl.GetAbsPos(pPara) > 0)
                            {
                                // not at top
                                bDisableUp = false;
                            }
                        }

                        for (const auto& rpItem : aSelList)
                        {
                            pPara = rpItem;

                            sal_Int16 nDepth = rOutl.GetDepth( rOutl.GetAbsPos( pPara ) );

                            if (nDepth > 0 || (bOutlineViewSh && (nDepth <= 0) && !::Outliner::HasParaFlag( pPara, ParaFlag::ISPAGE )) )
                            {
                                // not minimum depth
                                bDisableLeft = false;
                            }

                            if( (nDepth < pOLV->GetOutliner().GetMaxDepth() && ( !bOutlineViewSh || rOutl.GetAbsPos(pPara) != 0 )) ||
                                (bOutlineViewSh && (nDepth <= 0) && ::Outliner::HasParaFlag( pPara, ParaFlag::ISPAGE ) && rOutl.GetAbsPos(pPara) != 0) )
                            {
                                // not maximum depth and not at top
                                bDisableRight = false;
                            }
                        }

                        if ( ( rOutl.GetAbsPos(pPara) < rOutl.GetParagraphCount() - 1 ) &&
                             ( rOutl.GetParagraphCount() > 1 || !bOutlineViewSh) )
                        {
                            // not last paragraph
                            bDisableDown = false;
                        }

                        // disable when first para and 2nd is not a title
                        pPara = aSelList.empty() ? nullptr : *(aSelList.begin());

                        if(!bDisableDown && bIsOutlineView
                            && pPara
                            && 0 == rOutl.GetAbsPos(pPara)
                            && rOutl.GetParagraphCount() > 1
                            && !::Outliner::HasParaFlag( rOutl.GetParagraph(1), ParaFlag::ISPAGE ) )
                        {
                            // Needs to be disabled
                            bDisableDown = true;
                        }
                    }
                }

                if (bDisableLeft)
                    rSet.DisableItem(SID_OUTLINE_LEFT);
                if (bDisableRight)
                    rSet.DisableItem(SID_OUTLINE_RIGHT);
                if (bDisableUp)
                    rSet.DisableItem(SID_OUTLINE_UP);
                if (bDisableDown)
                    rSet.DisableItem(SID_OUTLINE_DOWN);
            }
            break;

            case SID_TEXTDIRECTION_LEFT_TO_RIGHT:
            case SID_TEXTDIRECTION_TOP_TO_BOTTOM:
            {
                if ( bDisableVerticalText )
                {
                    rSet.DisableItem( SID_TEXTDIRECTION_LEFT_TO_RIGHT );
                    rSet.DisableItem( SID_TEXTDIRECTION_TOP_TO_BOTTOM );
                }
                else
                {
                    bool bLeftToRight = true;

                    SdrOutliner* pOutl = pView->GetTextEditOutliner();
                    if( pOutl )
                    {
                        if( pOutl->IsVertical() )
                            bLeftToRight = false;
                    }
                    else
                        bLeftToRight = aAttrSet.Get( SDRATTR_TEXTDIRECTION ).GetValue() == css::text::WritingMode_LR_TB;

                    rSet.Put( SfxBoolItem( SID_TEXTDIRECTION_LEFT_TO_RIGHT, bLeftToRight ) );
                    rSet.Put( SfxBoolItem( SID_TEXTDIRECTION_TOP_TO_BOTTOM, !bLeftToRight ) );

                    if( !bLeftToRight )
                        bDisableParagraphTextDirection = true;
                }
            }
            break;

            case SID_ULINE_VAL_NONE:
            case SID_ULINE_VAL_SINGLE:
            case SID_ULINE_VAL_DOUBLE:
            case SID_ULINE_VAL_DOTTED:
            {
                if( aAttrSet.GetItemState( EE_CHAR_UNDERLINE ) >= SfxItemState::DEFAULT )
                {
                    FontLineStyle eLineStyle = aAttrSet.Get(EE_CHAR_UNDERLINE).GetLineStyle();

                    switch (nSlotId)
                    {
                        case SID_ULINE_VAL_NONE:
                            rSet.Put(SfxBoolItem(nSlotId, eLineStyle == LINESTYLE_NONE));
                            break;
                        case SID_ULINE_VAL_SINGLE:
                            rSet.Put(SfxBoolItem(nSlotId, eLineStyle == LINESTYLE_SINGLE));
                            break;
                        case SID_ULINE_VAL_DOUBLE:
                            rSet.Put(SfxBoolItem(nSlotId, eLineStyle == LINESTYLE_DOUBLE));
                            break;
                        case SID_ULINE_VAL_DOTTED:
                            rSet.Put(SfxBoolItem(nSlotId, eLineStyle == LINESTYLE_DOTTED));
                            break;
                    }
                }
            }
            break;

            case SID_GROW_FONT_SIZE:
            case SID_SHRINK_FONT_SIZE:
            {
                // todo
            }
            break;

            case SID_THES:
            {
                if (pView->GetTextEditOutlinerView())
                {
                    EditView & rEditView = pView->GetTextEditOutlinerView()->GetEditView();
                    OUString        aStatusVal;
                    LanguageType    nLang = LANGUAGE_NONE;
                    bool bIsLookUpWord = GetStatusValueForThesaurusFromContext( aStatusVal, nLang, rEditView );
                    rSet.Put( SfxStringItem( SID_THES, aStatusVal ) );

                    // disable "Thesaurus" context menu entry if there is nothing to look up
                    uno::Reference< linguistic2::XThesaurus > xThes( LinguMgr::GetThesaurus() );
                    if (!bIsLookUpWord ||
                        !xThes.is() || nLang == LANGUAGE_NONE || !xThes->hasLocale( LanguageTag( nLang). getLocale() ))
                        rSet.DisableItem( SID_THES );
                }
                else
                {
                    rSet.DisableItem( SID_THES );
                }
            }
            break;

            case FN_NUM_BULLET_ON:
            case FN_NUM_NUMBERING_ON:
            {
                bool bEnable = false;
                const DrawViewShell* pDrawViewShell = dynamic_cast< const DrawViewShell* >(&rViewShell);
                if (pDrawViewShell)
                {
                    SdrView* pDrawView = pDrawViewShell->GetDrawView();
                    //TODO: is pDrawView always available?
                    const SdrMarkList& rMarkList = pDrawView->GetMarkedObjectList();
                    const size_t nMarkCount = rMarkList.GetMarkCount();
                    for (size_t nIndex = 0; nIndex < nMarkCount; ++nIndex)
                    {
                        SdrTextObj* pTextObj = DynCastSdrTextObj(rMarkList.GetMark(nIndex)->GetMarkedSdrObj());
                        if (pTextObj && pTextObj->GetObjInventor() == SdrInventor::Default)
                        {
                            if (pTextObj->GetObjIdentifier() != SdrObjKind::OLE2)
                            {
                                bEnable = true;
                                break;
                            }
                        }
                    }
                    if (bEnable)
                    {
                        bool bIsBullet = false;
                        bool bIsNumbering = false;
                        OutlinerView* pOlView = pDrawView->GetTextEditOutlinerView();
                        if (pOlView)
                        {
                            pOlView->IsBulletOrNumbering(bIsBullet, bIsNumbering);
                        }
                        rSet.Put(SfxBoolItem(FN_NUM_BULLET_ON, bIsBullet));
                        rSet.Put(SfxBoolItem(FN_NUM_NUMBERING_ON, bIsNumbering));
                    }
                    else
                    {
                        rSet.DisableItem(FN_NUM_BULLET_ON);
                        rSet.DisableItem(FN_NUM_NUMBERING_ON);
                    }
                }
            }
            break;
            default:
            break;
        }

        nWhich = aIter.NextWhich();
    }

    rSet.Put( aAttrSet, false ); // <- sal_False, so DontCare-Status gets acquired

    // these are disabled in outline-mode
    if (!(dynamic_cast<const DrawViewShell*>(&rViewShell)
             || dynamic_cast<const NotesPanelViewShell*>(&rViewShell)))
    {
        rSet.DisableItem( SID_ATTR_PARA_ADJUST_LEFT );
        rSet.DisableItem( SID_ATTR_PARA_ADJUST_RIGHT );
        rSet.DisableItem( SID_ATTR_PARA_ADJUST_CENTER );
        rSet.DisableItem( SID_ATTR_PARA_ADJUST_BLOCK );
        rSet.DisableItem( SID_ATTR_PARA_LINESPACE_10 );
        rSet.DisableItem( SID_ATTR_PARA_LINESPACE_15 );
        rSet.DisableItem( SID_ATTR_PARA_LINESPACE_20 );
        rSet.DisableItem( SID_DEC_INDENT );
        rSet.DisableItem( SID_INC_INDENT );
        rSet.DisableItem( SID_PARASPACE_INCREASE );
        rSet.DisableItem( SID_PARASPACE_DECREASE );
        rSet.DisableItem( SID_TEXTDIRECTION_TOP_TO_BOTTOM );
        rSet.DisableItem( SID_TEXTDIRECTION_LEFT_TO_RIGHT );
        rSet.DisableItem( SID_ATTR_PARA_LEFT_TO_RIGHT );
        rSet.DisableItem( SID_ATTR_PARA_RIGHT_TO_LEFT );
    }
    else
    {
        // paragraph spacing
        OutlinerView* pOLV = pView->GetTextEditOutlinerView();
        if( pOLV )
        {
            ESelection aSel = pOLV->GetSelection();
            aSel.Adjust();
            sal_Int32 nStartPara = aSel.start.nPara;
            sal_Int32 nEndPara = aSel.end.nPara;
            if( !aSel.HasRange() )
            {
                nStartPara = 0;
                nEndPara = pOLV->GetOutliner().GetParagraphCount() - 1;
            }
            ::tools::Long nUpper = 0;

            for( sal_Int32 nPara = nStartPara; nPara <= nEndPara; nPara++ )
            {
                const SfxItemSet& rItems = pOLV->GetOutliner().GetParaAttribs( nPara );
                const SvxULSpaceItem& rItem = rItems.Get( EE_PARA_ULSPACE );
                nUpper = std::max( nUpper, static_cast<::tools::Long>(rItem.GetUpper()) );
            }
            if( nUpper == 0 )
                rSet.DisableItem( SID_PARASPACE_DECREASE );
        }
        else
        {
            // never disabled at the moment!
            //rSet.DisableItem( SID_PARASPACE_INCREASE );
            //rSet.DisableItem( SID_PARASPACE_DECREASE );
        }

        // paragraph justification
        const SvxLRSpaceItem& aLR = aAttrSet.Get( EE_PARA_LRSPACE );
        rSet.Put(aLR);
        SvxAdjust eAdj = aAttrSet.Get( EE_PARA_JUST ).GetAdjust();
        switch( eAdj )
        {
            case SvxAdjust::Left:
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_LEFT, true ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_CENTER, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_RIGHT, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_BLOCK, false ) );
                break;
            case SvxAdjust::Center:
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_CENTER, true ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_LEFT, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_RIGHT, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_BLOCK, false ) );
                break;
            case SvxAdjust::Right:
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_RIGHT, true ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_CENTER, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_LEFT, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_BLOCK, false ) );
                break;
            case SvxAdjust::Block:
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_BLOCK, true ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_CENTER, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_RIGHT, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_LEFT, false ) );
                break;
            default:
            break;
        }

        if(pTextObjectBar)
        {
            pTextObjectBar->Invalidate(SID_ATTR_PARA_ADJUST_LEFT);
            pTextObjectBar->Invalidate(SID_ATTR_PARA_ADJUST_CENTER);
            pTextObjectBar->Invalidate(SID_ATTR_PARA_ADJUST_RIGHT);
            pTextObjectBar->Invalidate(SID_ATTR_PARA_ADJUST_BLOCK);
            pTextObjectBar->Invalidate(SID_ATTR_PARA_LINESPACE);
            pTextObjectBar->Invalidate(SID_ATTR_PARA_ULSPACE);
        }

        // paragraph text direction
        if( bDisableParagraphTextDirection )
        {
            rSet.DisableItem( SID_ATTR_PARA_LEFT_TO_RIGHT );
            rSet.DisableItem( SID_ATTR_PARA_RIGHT_TO_LEFT );
        }
        else
        {
            switch( aAttrSet.Get( EE_PARA_WRITINGDIR ).GetValue() )
            {
                case SvxFrameDirection::Vertical_LR_TB:
                case SvxFrameDirection::Vertical_RL_TB:
                {
                    rSet.DisableItem( SID_ATTR_PARA_LEFT_TO_RIGHT );
                    rSet.DisableItem( SID_ATTR_PARA_RIGHT_TO_LEFT );
                }
                break;

                case SvxFrameDirection::Horizontal_LR_TB:
                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_LEFT_TO_RIGHT, true ) );
                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_RIGHT_TO_LEFT, false ) );
                break;

                case SvxFrameDirection::Horizontal_RL_TB:
                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_LEFT_TO_RIGHT, false ) );
                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_RIGHT_TO_LEFT, true ) );
                break;

                // The case for the superordinate object is missing.
                case SvxFrameDirection::Environment:
                {
                    SdDrawDocument& rDoc = pView->GetDoc();
                    css::text::WritingMode eMode = rDoc.GetDefaultWritingMode();
                    bool bIsLeftToRight(false);

                    if(css::text::WritingMode_LR_TB == eMode
                        || css::text::WritingMode_TB_RL == eMode)
                    {
                        bIsLeftToRight = true;
                    }

                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_LEFT_TO_RIGHT, bIsLeftToRight ) );
                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_RIGHT_TO_LEFT, !bIsLeftToRight ) );
                }
                break;
                default: break;
            }
        }

        SvxLRSpaceItem aLRSpace = aAttrSet.Get( EE_PARA_LRSPACE );
        aLRSpace.SetWhich(SID_ATTR_PARA_LRSPACE);
        rSet.Put(aLRSpace);
        if (pTextObjectBar)
            pTextObjectBar->Invalidate(SID_ATTR_PARA_LRSPACE);

        //Added by xuxu
        SfxItemState eState = aAttrSet.GetItemState( EE_PARA_LRSPACE );
        if ( eState == SfxItemState::INVALID )
        {
            rSet.InvalidateItem(EE_PARA_LRSPACE);
            rSet.InvalidateItem(SID_ATTR_PARA_LRSPACE);
        }
        sal_uInt16 nLineSpace = aAttrSet.Get( EE_PARA_SBL ).GetPropLineSpace();
        switch( nLineSpace )
        {
            case 100:
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_LINESPACE_10, true ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_LINESPACE_15, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_LINESPACE_20, false ) );
            break;
            case 150:
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_LINESPACE_15, true ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_LINESPACE_10, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_LINESPACE_20, false ) );
            break;
            case 200:
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_LINESPACE_20, true ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_LINESPACE_10, false ) );
                rSet.Put( SfxBoolItem( SID_ATTR_PARA_LINESPACE_15, false ) );
            break;
        }
    }

    // justification (superscript, subscript) is also needed in outline-mode
    SvxEscapement eEsc = aAttrSet.Get(EE_CHAR_ESCAPEMENT).GetEscapement();
    rSet.Put(SfxBoolItem(SID_SET_SUPER_SCRIPT, eEsc == SvxEscapement::Superscript));
    rSet.Put(SfxBoolItem(SID_SET_SUB_SCRIPT, eEsc == SvxEscapement::Subscript));

    SvxCaseMap eCaseMap = aAttrSet.Get(EE_CHAR_CASEMAP).GetCaseMap();
    rSet.Put(SfxBoolItem(SID_SET_SMALL_CAPS, eCaseMap == SvxCaseMap::SmallCaps));
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
