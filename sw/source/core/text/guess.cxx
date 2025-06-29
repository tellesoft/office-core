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

#include <editeng/unolingu.hxx>
#include <breakit.hxx>
#include <IDocumentSettingAccess.hxx>
#include "guess.hxx"
#include "inftxt.hxx"
#include <pagefrm.hxx>
#include <tgrditem.hxx>
#include <com/sun/star/i18n/BreakType.hpp>
#include <com/sun/star/i18n/WordType.hpp>
#include <com/sun/star/i18n/XBreakIterator.hpp>
#include <com/sun/star/text/ParagraphHyphenationKeepType.hpp>
#include <unotools/charclass.hxx>
#include <svl/urihelper.hxx>
#include "porfld.hxx"
#include <paratr.hxx>
#include <doc.hxx>
#include <unotools/linguprops.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::i18n;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::linguistic2;

namespace{

bool IsBlank(sal_Unicode ch) { return ch == CH_BLANK || ch == CH_FULL_BLANK || ch == CH_NB_SPACE || ch == CH_SIX_PER_EM; }

// Used when spaces should not be counted in layout
// Returns adjusted cut position
TextFrameIndex AdjustCutPos(TextFrameIndex cutPos, TextFrameIndex& rBreakPos,
                            const SwTextFormatInfo& rInf)
{
    assert(cutPos >= rInf.GetIdx());
    TextFrameIndex x = rBreakPos = cutPos;

    // we step back until a non blank character has been found
    // or there is only one more character left
    while (x && x > rInf.GetIdx() + TextFrameIndex(1) && IsBlank(rInf.GetChar(--x)))
        --rBreakPos;

    while (IsBlank(rInf.GetChar(cutPos)))
        ++cutPos;

    return cutPos;
}

bool hasBlanksInLine(const SwTextFormatInfo& rInf, TextFrameIndex end)
{
    for (auto x = rInf.GetLineStart(); x < end; ++x)
        if (IsBlank(rInf.GetChar(x)))
            return true;
    return false;
}

}

// Called for the last text run in a line; if it is block-adjusted, or center / right-adjusted
// with Word compatibility option set, and it has trailing spaces, then the function sets the
// values, and returns 'false' value that SwTextGuess::Guess should return, to create a
// trailing SwHolePortion.
bool SwTextGuess::maybeAdjustPositionsForBlockAdjust(tools::Long& rMaxSizeDiff,
                                                     SwTwips& rExtraAscent, SwTwips& rExtraDescent,
                                                     const SwTextFormatInfo& rInf, const SwScriptInfo& rSI,
                                                     sal_uInt16 maxComp,
                                                     std::optional<SwLinePortionLayoutContext> nLayoutContext)
{
    const auto& adjObj = rInf.GetTextFrame()->GetTextNodeForParaProps()->GetSwAttrSet().GetAdjust();
    const SvxAdjust adjust = adjObj.GetAdjust();
    if (adjust == SvxAdjust::Block)
    {
        if (rInf.DontBlockJustify())
            return true; // See tdf#106234
    }
    else
    {
        // tdf#104668 space chars at the end should be cut if the compatibility option is enabled
        if (!rInf.GetTextFrame()->GetDoc().getIDocumentSettingAccess().get(
                DocumentSettingId::MS_WORD_COMP_TRAILING_BLANKS))
            return true;
        // for LTR mode only
        if (rInf.GetTextFrame()->IsRightToLeft())
            return true;
    }
    if (auto ch = rInf.GetChar(m_nCutPos); !ch) // end of paragraph - last line
    {
        if (adjust == SvxAdjust::Block)
        {
            // Check adjustment for last line
            switch (adjObj.GetLastBlock())
            {
                default:
                    return true;
                case SvxAdjust::Center: // tdf#104668
                    if (!rInf.GetTextFrame()->GetDoc().getIDocumentSettingAccess().get(
                            DocumentSettingId::MS_WORD_COMP_TRAILING_BLANKS))
                        return true;
                    break;
                case SvxAdjust::Block:
                    break; // OK - last line uses block-adjustment
            }
        }
    }
    else if (ch != CH_BREAK && !IsBlank(ch))
        return true;

    // tdf#57187: block-adjusted line shorter than full width, terminated by manual
    // line break, must not use trailing spaces for adjustment
    TextFrameIndex breakPos;
    TextFrameIndex newCutPos = AdjustCutPos(m_nCutPos, breakPos, rInf);

    if (auto ch = rInf.GetChar(newCutPos); ch && ch != CH_BREAK)
        return true; // next is neither line break nor paragraph end
    if (breakPos == newCutPos)
        return true; // no trailing whitespace
    if (adjust == SvxAdjust::Block && adjObj.GetOneWord() != SvxAdjust::Block
        && !hasBlanksInLine(rInf, breakPos))
        return true; // line can't block-adjust

    // Some trailing spaces actually found, and in case of block adjustment, the text portion
    // itself has spaces to be able to block-adjust, or single word is allowed to adjust
    m_nBreakStart = m_nCutPos = newCutPos;
    m_nBreakPos = breakPos;
    // throw away old m_xHyphWord because the current break pos is now between words
    m_xHyphWord = nullptr;

    rInf.GetTextSize(&rSI, rInf.GetIdx(), breakPos - rInf.GetIdx(), nLayoutContext, maxComp,
                     m_nBreakWidth, rMaxSizeDiff, rExtraAscent, rExtraDescent,
                     rInf.GetCachedVclData().get());
    rInf.GetTextSize(&rSI, breakPos, m_nBreakStart - breakPos, nLayoutContext, maxComp,
                     m_nExtraBlankWidth, rMaxSizeDiff, rExtraAscent, rExtraDescent,
                     rInf.GetCachedVclData().get());

    return false; // require SwHolePortion creation
}

// provides information for line break calculation
// returns true if no line break has to be performed
// otherwise possible break or hyphenation position is determined
bool SwTextGuess::Guess( const SwTextPortion& rPor, SwTextFormatInfo &rInf,
                            const sal_uInt16 nPorHeight, sal_Int32 nSpacesInLine,
                            sal_uInt16 nPropWordSpacing, sal_Int16 nSpaceWidth )
{
    m_nCutPos = rInf.GetIdx();

    // Empty strings are always 0
    if( !rInf.GetLen() || rInf.GetText().isEmpty() )
        return false;

    OSL_ENSURE( rInf.GetIdx() < TextFrameIndex(rInf.GetText().getLength()),
            "+SwTextGuess::Guess: invalid SwTextFormatInfo" );

    OSL_ENSURE( nPorHeight, "+SwTextGuess::Guess: no height" );

    tools::Long nMaxSizeDiff;
    SwTwips nExtraAscent = 0;
    SwTwips nExtraDescent = 0;

    const SwScriptInfo& rSI = rInf.GetParaPortion()->GetScriptInfo();

    const sal_uInt16 nMaxComp = rPor.GetMaxComp(rInf);

    SwTwips nLineWidth = rInf.GetLineWidth();
    TextFrameIndex nMaxLen = TextFrameIndex(rInf.GetText().getLength()) - rInf.GetIdx();

    SvxAdjustItem aAdjustItem = rInf.GetTextFrame()->GetTextNodeForParaProps()->GetSwAttrSet().GetAdjust();
    const SvxAdjust aAdjust = aAdjustItem.GetAdjust();
    // Maximum word spacing allows bigger spaces to limit hyphenation,
    // implement it based on the hyphenation zone: calculate a hyphenation zone
    // from maximum word spacing and space count of the line
    SwTwips nWordSpacingMaximumZone = 0;

    if ( nSpacesInLine )
    {
        SwTwips nExtraSpace = nSpacesInLine * nSpaceWidth/10.0 * (1.0 - nPropWordSpacing / 100.0);
        nLineWidth += nExtraSpace;
        // convert maximum word spacing to hyphenation zone, if defined
        if ( nPropWordSpacing == aAdjustItem.GetPropWordSpacing() )
        {
            SwTwips nMaxDif = aAdjustItem.GetPropWordSpacingMaximum() - nPropWordSpacing;
            nWordSpacingMaximumZone = nSpacesInLine * nSpaceWidth/10.0 * nMaxDif / 100.0;
        }

        rInf.SetExtraSpace(nExtraSpace);
    }

    if ( rInf.GetLen() < nMaxLen )
        nMaxLen = rInf.GetLen();

    if( !nMaxLen )
        return false;

    sal_uInt16 nItalic = 0;
    if( ITALIC_NONE != rInf.GetFont()->GetItalic() && !rInf.NotEOL() )
    {
        bool bAddItalic = true;

        // do not add extra italic value if we have an active character grid
        if ( rInf.SnapToGrid() )
        {
            SwTextGridItem const*const pGrid(
                    GetGridItem(rInf.GetTextFrame()->FindPageFrame()));
            bAddItalic = !pGrid || SwTextGrid::LinesAndChars != pGrid->GetGridType();
        }

        // do not add extra italic value for an isolated blank:
        if (TextFrameIndex(1) == rInf.GetLen() &&
            CH_BLANK == rInf.GetText()[sal_Int32(rInf.GetIdx())])
        {
            bAddItalic = false;
        }

        if (rInf.GetTextFrame()->GetDoc().getIDocumentSettingAccess().get(
                DocumentSettingId::TAB_OVER_MARGIN))
        {
            // Content is allowed over the margin: in this case over-margin content caused by italic
            // formatting is OK.
            bAddItalic = false;
        }

        nItalic = bAddItalic ? nPorHeight / 12 : 0;

        nLineWidth -= nItalic;

        // #i46524# LineBreak bug with italics
        if ( nLineWidth < 0 ) nLineWidth = 0;
    }

    const sal_Int32 nLeftRightBorderSpace =
        (!rPor.GetJoinBorderWithNext() ? rInf.GetFont()->GetRightBorderSpace() : 0) +
        (!rPor.GetJoinBorderWithPrev() ? rInf.GetFont()->GetLeftBorderSpace() : 0);

    nLineWidth -= nLeftRightBorderSpace;

    const bool bUnbreakableNumberings = rInf.GetTextFrame()->GetDoc()
        .getIDocumentSettingAccess().get(DocumentSettingId::UNBREAKABLE_NUMBERINGS);

    // first check if everything fits to line
    if ( ( nLineWidth * 2 > SwTwips(sal_Int32(nMaxLen)) * nPorHeight ) ||
         ( bUnbreakableNumberings && rPor.IsNumberPortion() ) )
    {
        // call GetTextSize with maximum compression (for kanas)
        rInf.GetTextSize(&rSI, rInf.GetIdx(), nMaxLen, rInf.GetLayoutContext(), nMaxComp,
                         m_nBreakWidth, nMaxSizeDiff, nExtraAscent, nExtraDescent);

        if ( ( m_nBreakWidth <= nLineWidth ) || ( bUnbreakableNumberings && rPor.IsNumberPortion() ) )
        {
            // portion fits to line
            m_nCutPos = rInf.GetIdx() + nMaxLen;
            bool bRet = rPor.InFieldGrp()
                        || maybeAdjustPositionsForBlockAdjust(
                            nMaxSizeDiff, nExtraAscent, nExtraDescent, rInf,
                            rSI, nMaxComp, rInf.GetLayoutContext());
            if( nItalic &&
                (m_nCutPos >= TextFrameIndex(rInf.GetText().getLength()) ||
                  // #i48035# Needed for CalcFitToContent
                  // if first line ends with a manual line break
                  rInf.GetText()[sal_Int32(m_nCutPos)] == CH_BREAK))
                m_nBreakWidth += nItalic;

            // save maximum width for later use
            if ( nMaxSizeDiff )
                rInf.SetMaxWidthDiff( &rPor, nMaxSizeDiff );

            rInf.SetExtraAscent(nExtraAscent);
            rInf.SetExtraDescent(nExtraDescent);

            m_nBreakWidth += nLeftRightBorderSpace;

            return bRet;
        }
    }

    bool bHyph = rInf.IsHyphenate() && !rInf.IsHyphForbud() &&
            // disable hyphenation at minimum word spacing
            // (and at weighted word spacing between minimum and desired word spacing)
            !( nPropWordSpacing < aAdjustItem.GetPropWordSpacing() );

    // disable hyphenation according to hyphenation-keep and hyphenation-keep-type,
    // or modify hyphenation according to hyphenation-zone-column/page/spread (see widorp.cxx)
    sal_Int16 nEndZone = 0;
    if ( bHyph &&
          rInf.GetTextFrame()->GetNoHyphOffset() != TextFrameIndex(COMPLETE_STRING) && // ) // &&
          // when there is a portion in the last line, rInf.GetIdx() > GetNoHyphOffset()
          rInf.GetTextFrame()->GetNoHyphOffset() <= rInf.GetIdx() )
    {
          nEndZone = rInf.GetTextFrame()->GetNoHyphEndZone();
          // disable hyphenation in the last line, when hyphenation-keep-line is enabled
          // and hyphenation-keep, too (i.e. when end zone is not defined),
          // also when the end zone is bigger, than the line width
          if ( nEndZone < 0 || nEndZone >= nLineWidth )
              bHyph = false;
    }
    TextFrameIndex nHyphPos(0);

    // nCutPos is the first character not fitting to the current line
    // nHyphPos is the first character not fitting to the current line,
    // considering an additional "-" for hyphenation
    if( bHyph )
    {
        // nHyphZone is the first character not fitting in the hyphenation zone,
        // or 0, if the whole line in the hyphenation zone,
        // or -1, if no hyphenation zone defined (i.e. it is 0)
        sal_Int32 nHyphZone = -1;
        sal_Int32 nParaZone = -1;
        const css::beans::PropertyValues & rHyphValues = rInf.GetHyphValues();
        assert( rHyphValues.getLength() > 10 && rHyphValues[5].Name == UPN_HYPH_ZONE && rHyphValues[10].Name == UPN_HYPH_ZONE_ALWAYS );
        // hyphenation zone (distance from the line end in twips)
        sal_Int16 nTextHyphenZone = 0;
        sal_Int16 nTextHyphenZoneAlways = 0;
        rHyphValues[5].Value >>= nTextHyphenZone;

        // maximum word spacing can result bigger hyphenation zone,
        // if there are enough spaces in the line: apply that
        if ( nWordSpacingMaximumZone > nTextHyphenZone )
            nTextHyphenZone = nWordSpacingMaximumZone;

        rHyphValues[10].Value >>= nTextHyphenZoneAlways;
        if ( nTextHyphenZone || nTextHyphenZoneAlways || nEndZone )
        {
            nHyphZone = nTextHyphenZone >= nLineWidth
                ? 0
                : sal_Int32(rInf.GetTextBreak( nLineWidth - (nEndZone ? nEndZone : nTextHyphenZone),
                                        nMaxLen, nMaxComp, rInf.GetCachedVclData().get() ));
        }
        m_nCutPos = rInf.GetTextBreak( nLineWidth, nMaxLen, nMaxComp, nHyphPos, rInf.GetCachedVclData().get() );

        if ( !nEndZone && nTextHyphenZoneAlways &&
               // if paragraph end zone is not different from the hyphenation zone, skip its handling
               nTextHyphenZoneAlways != nTextHyphenZone &&
               // end of the paragraph
               rInf.GetText().getLength() - sal_Int32(nHyphZone) <
               sal_Int32(m_nCutPos) - sal_Int32(rInf.GetLineStart() ) )
        {
            nParaZone = nTextHyphenZoneAlways >= nLineWidth
                ? 0
                : sal_Int32(rInf.GetTextBreak( nLineWidth - nTextHyphenZoneAlways,
                                        nMaxLen, nMaxComp, rInf.GetCachedVclData().get() ));
        }

        // don't try to hyphenate in the hyphenation zone or in the paragraph end zone
        // maximum end zone is the lower non-negative text break position
        // (-1 means that no hyphenation zone is defined)
        sal_Int32 nMaxZone = nParaZone > -1 && nParaZone < nHyphZone
                ? nParaZone
                : nHyphZone;
        if ( nMaxZone != -1 && TextFrameIndex(COMPLETE_STRING) != m_nCutPos )
        {
            sal_Int32 nZonePos = sal_Int32(m_nCutPos);
            // disable hyphenation, if there is a space within the hyphenation or end zones
            // Note: for better interoperability, not fitting space character at
            // rInf.GetIdx()[nHyphZone] always disables the hyphenation, don't need to calculate
            // with its fitting part. Moreover, do not check double or more spaces there, they
            // are accepted outside of the hyphenation zone, too.
            for (; sal_Int32(rInf.GetLineStart()) <= nZonePos && nMaxZone <= nZonePos; --nZonePos )
            {
                sal_Unicode cChar = rInf.GetText()[nZonePos];
                if ( cChar == CH_BLANK || cChar == CH_FULL_BLANK || cChar == CH_SIX_PER_EM )
                {
                    // no column/page/spread/end zone (!nEndZone),
                    // but is it good for a paragraph end zone?
                    if ( nParaZone != -1 && nParaZone <= nZonePos &&
                        // still end of the paragraph, i.e. still more characters in the original
                        // last full line, then in the planned last paragraph line
                        // FIXME: guarantee, that last not full line won't become a full line
                        rInf.GetText().getLength() - sal_Int32(nZonePos) <
                            sal_Int32(m_nCutPos) - sal_Int32(rInf.GetLineStart() ) )
                    {
                        bHyph = false;
                    }
                    // otherwise disable hyphenation, if there is a space in the hyphenation zone
                    else if ( nHyphZone != 1 && nHyphZone <= nZonePos )
                    {
                        bHyph = false;
                    }
                    // set applied end zone
                    if ( !bHyph && nEndZone )
                        rInf.GetTextFrame()->SetNoHyphOffset(TextFrameIndex(COMPLETE_STRING));
                }
            }
        }

        if (!rInf.GetTextFrame()->GetDoc().getIDocumentSettingAccess().get(
                    DocumentSettingId::HYPHENATE_URLS))
        {
            // check URL from preceding space - similar to what AutoFormat does
            const CharClass& rCC = GetAppCharClass();
            sal_Int32 begin(m_nCutPos == TextFrameIndex(COMPLETE_STRING) ? rInf.GetText().getLength() : sal_Int32(m_nCutPos));
            sal_Int32 end(begin);
            for (; 0 < begin; --begin)
            {
                sal_Unicode cChar = rInf.GetText()[begin - 1];
                if (cChar == CH_BLANK || cChar == CH_FULL_BLANK || cChar == CH_SIX_PER_EM)
                {
                    break;
                }
            }
            for (; end < rInf.GetText().getLength(); ++end)
            {
                sal_Unicode cChar = rInf.GetText()[end];
                if (cChar == CH_BLANK || cChar == CH_FULL_BLANK || cChar == CH_SIX_PER_EM)
                {
                    break;
                }
            }
            if (!URIHelper::FindFirstURLInText(rInf.GetText(), begin, end, rCC).isEmpty())
            {
                bHyph = false;
            }
        }

        // search start of the last word, if needed
        if ( bHyph )
        {
            // nLastWord is the space character before the last word of the line
            sal_Int32 nLastWord = rInf.GetText().getLength() - 1;
            bool bDoNotHyphenateLastLine = false; // don't hyphenate last full line of the paragraph
            bool bHyphenationNoLastWord = false;  // do not hyphenate the last word of the paragraph
            assert( rHyphValues.getLength() > 3 && rHyphValues[3].Name == UPN_HYPH_NO_LAST_WORD );
            assert( rHyphValues.getLength() > 6 && rHyphValues[6].Name == UPN_HYPH_KEEP_TYPE );
            assert( rHyphValues.getLength() > 8 && rHyphValues[8].Name == UPN_HYPH_KEEP );
            rHyphValues[3].Value >>= bHyphenationNoLastWord;
            rHyphValues[8].Value >>= bDoNotHyphenateLastLine;
            if ( bDoNotHyphenateLastLine )
            {
                sal_Int16 nKeepType = css::text::ParagraphHyphenationKeepType::COLUMN;
                rHyphValues[6].Value >>= nKeepType;
                if ( nKeepType == css::text::ParagraphHyphenationKeepType::ALWAYS )
                {
                    if ( TextFrameIndex(COMPLETE_STRING) != m_nCutPos )
                        nLastWord = sal_Int32(m_nCutPos);
                }
                else
                    bDoNotHyphenateLastLine = false;
            }

            if ( bHyphenationNoLastWord || bDoNotHyphenateLastLine )
            {
                // skip spaces after the last word
                bool bCutBlank = false;
                for (; sal_Int32(rInf.GetIdx()) <= nLastWord; --nLastWord )
                {
                    sal_Unicode cChar = rInf.GetText()[nLastWord];
                    if ( cChar != CH_BLANK && cChar != CH_FULL_BLANK && cChar != CH_SIX_PER_EM )
                        bCutBlank = true;
                    else if ( bCutBlank )
                        break;
                }
            }

            // don't hyphenate the last word of the paragraph line
            if ( ( bHyphenationNoLastWord || bDoNotHyphenateLastLine ) &&
                            sal_Int32(m_nCutPos) > nLastWord &&
                            TextFrameIndex(COMPLETE_STRING) != m_nCutPos &&
                            // if the last word is multiple line long, e.g. an URL,
                            // apply this only if the space before the word is there
                            // in the actual line, i.e. start the long word in a new
                            // line, but still allows to break its last parts
                            sal_Int32(rInf.GetLineStart()) < nLastWord &&
                            // if the case of bDoNotHyphenateLastLine == true, skip hyphenation
                            // only if the character length of the very last line of the paragraph
                            // would be still less, than the length of the recent last but one line
                            // with hyphenation, i.e. don't skip hyphenation, if the last paragraph
                            // line is already near full.
                            ( !bDoNotHyphenateLastLine ||
                                  // FIXME: character count is not fail-safe: remaining characters
                                  // can exceed the line, resulting two last full paragraph lines
                                  // with disabled hyphenation.
                                  rInf.GetText().getLength() - sal_Int32(nLastWord) <
                                      sal_Int32(m_nCutPos) - sal_Int32(rInf.GetLineStart() ) ) )
            {
                m_nCutPos = TextFrameIndex(nLastWord);
            }
        }

        if ( !nHyphPos && rInf.GetIdx() )
            nHyphPos = rInf.GetIdx() - TextFrameIndex(1);
    }
    else
    {
        m_nCutPos = rInf.GetTextBreak( nLineWidth, nMaxLen, nMaxComp, rInf.GetCachedVclData().get() );

#if OSL_DEBUG_LEVEL > 1
        if ( TextFrameIndex(COMPLETE_STRING) != m_nCutPos )
        {
            SwTwips nMinSize;
            rInf.GetTextSize(&rSI, rInf.GetIdx(), m_nCutPos - rInf.GetIdx(), std::nullopt, nMaxComp,
                             nMinSize, nMaxSizeDiff, nExtraAscent, nExtraDescent);
            OSL_ENSURE( nMinSize <= nLineWidth, "What a Guess!!!" );
        }
#endif
    }

    if( m_nCutPos > rInf.GetIdx() + nMaxLen )
    {
        // second check if everything fits to line
        m_nCutPos = m_nBreakPos = rInf.GetIdx() + nMaxLen - TextFrameIndex(1);
        rInf.GetTextSize(&rSI, rInf.GetIdx(), nMaxLen, rInf.GetLayoutContext(), nMaxComp,
                         m_nBreakWidth, nMaxSizeDiff, nExtraAscent, nExtraDescent);

        // The following comparison should always give true, otherwise
        // there likely has been a pixel rounding error in GetTextBreak
        if ( m_nBreakWidth <= nLineWidth )
        {
            bool bRet = rPor.InFieldGrp()
                        || maybeAdjustPositionsForBlockAdjust(
                            nMaxSizeDiff, nExtraAscent, nExtraDescent, rInf,
                            rSI, nMaxComp, rInf.GetLayoutContext());

            if (nItalic && (m_nBreakPos + TextFrameIndex(1)) >= TextFrameIndex(rInf.GetText().getLength()))
                m_nBreakWidth += nItalic;

            // save maximum width for later use
            if ( nMaxSizeDiff )
                rInf.SetMaxWidthDiff( &rPor, nMaxSizeDiff );

            rInf.SetExtraAscent(nExtraAscent);
            rInf.SetExtraDescent(nExtraDescent);

            m_nBreakWidth += nLeftRightBorderSpace;

            return bRet;
        }
    }

    // we have to trigger an underflow for a footnote portion
    // which does not fit to the current line
    if ( rPor.IsFootnotePortion() )
    {
        m_nBreakPos = rInf.GetIdx();
        m_nCutPos = TextFrameIndex(-1);
        return false;
    }

    TextFrameIndex nPorLen(0);
    // do not call the break iterator nCutPos is a blank
    sal_Unicode cCutChar = rInf.GetChar(m_nCutPos);
    if (IsBlank(cCutChar))
    {
        m_nCutPos = m_nBreakStart = AdjustCutPos(m_nCutPos, m_nBreakPos, rInf);
        nPorLen = m_nBreakPos - rInf.GetIdx();
        // throw away old m_xHyphWord when m_nBreakStart changes
        m_xHyphWord = nullptr;
    }
    else
    {
        // New: We should have a look into the last portion, if it was a
        // field portion. For this, we expand the text of the field portion
        // into our string. If the line break position is inside of before
        // the field portion, we trigger an underflow.

        TextFrameIndex nOldIdx = rInf.GetIdx();
        sal_Unicode cFieldChr = 0;

#if OSL_DEBUG_LEVEL > 0
        OUString aDebugString;
#endif

        // be careful: a field portion can be both: 0x01 (common field)
        // or 0x02 (the follow of a footnode)
        if ( rInf.GetLast() && rInf.GetLast()->InFieldGrp() &&
             ! rInf.GetLast()->IsFootnotePortion() &&
             rInf.GetIdx() > rInf.GetLineStart() &&
             CH_TXTATR_BREAKWORD ==
                (cFieldChr = rInf.GetText()[sal_Int32(rInf.GetIdx()) - 1]))
        {
            SwFieldPortion* pField = static_cast<SwFieldPortion*>(rInf.GetLast());
            OUString aText;
            pField->GetExpText( rInf, aText );

            if ( !aText.isEmpty() )
            {
                m_nFieldDiff = TextFrameIndex(aText.getLength() - 1);
                m_nCutPos = m_nCutPos + m_nFieldDiff;
                nHyphPos = nHyphPos + m_nFieldDiff;

#if OSL_DEBUG_LEVEL > 0
                aDebugString = rInf.GetText();
#endif

                // this is pretty nutso... reverted at the end...
                OUString& rOldText = const_cast<OUString&> (rInf.GetText());
                rOldText = rOldText.replaceAt(sal_Int32(rInf.GetIdx()) - 1, 1, aText);
                rInf.SetIdx( rInf.GetIdx() + m_nFieldDiff );
            }
            else
                cFieldChr = 0;
        }

        LineBreakHyphenationOptions aHyphOpt;
        Reference< XHyphenator >  xHyph;
        if( bHyph )
        {
            xHyph = ::GetHyphenator();
            aHyphOpt = LineBreakHyphenationOptions( xHyph,
                                rInf.GetHyphValues(), sal_Int32(nHyphPos));
        }

        // Get Language for break iterator.
        // We have to switch the current language if we have a script
        // change at nCutPos. Otherwise LATIN punctuation would never
        // be allowed to be hanging punctuation.
        // NEVER call GetLangOfChar if the string has been modified!!!
        LanguageType aLang = rInf.GetFont()->GetLanguage();

        // If we are inside a field portion, we use a temporary string which
        // differs from the string at the textnode. Therefore we are not allowed
        // to call the GetLangOfChar function.
        if ( m_nCutPos && ! rPor.InFieldGrp() )
        {
            const CharClass& rCC = GetAppCharClass();

            // step back until a non-punctuation character is reached
            TextFrameIndex nLangIndex = m_nCutPos;

            // If a field has been expanded right in front of us we do not
            // step further than the beginning of the expanded field
            // (which is the position of the field placeholder in our
            // original string).
            const TextFrameIndex nDoNotStepOver = CH_TXTATR_BREAKWORD == cFieldChr
                    ? rInf.GetIdx() - m_nFieldDiff - TextFrameIndex(1)
                    : TextFrameIndex(0);

            if ( nLangIndex > nDoNotStepOver &&
                    TextFrameIndex(rInf.GetText().getLength()) == nLangIndex)
                --nLangIndex;

            while ( nLangIndex > nDoNotStepOver &&
                    !rCC.isLetterNumeric(rInf.GetText(), sal_Int32(nLangIndex)))
                --nLangIndex;

            // last "real" character is not inside our current portion
            // we have to check the script type of the last "real" character
            if ( nLangIndex < rInf.GetIdx() )
            {
                sal_uInt16 nScript = g_pBreakIt->GetRealScriptOfText( rInf.GetText(),
                                        sal_Int32(nLangIndex));
                OSL_ENSURE( nScript, "Script is not between 1 and 4" );

                // compare current script with script from last "real" character
                if ( SwFontScript(nScript - 1) != rInf.GetFont()->GetActual() )
                {
                    aLang = rInf.GetTextFrame()->GetLangOfChar(
                        CH_TXTATR_BREAKWORD == cFieldChr
                            ? nDoNotStepOver
                            : nLangIndex,
                        nScript, true);
                }
            }
        }

        const ForbiddenCharacters aForbidden(
            *rInf.GetTextFrame()->GetDoc().getIDocumentSettingAccess().getForbiddenCharacters(aLang, true));

        const bool bAllowHanging = rInf.IsHanging() && ! rInf.IsMulti() &&
                                      ! rInf.GetTextFrame()->IsInTab() &&
                                      ! rPor.InFieldGrp();

        LineBreakUserOptions aUserOpt(
                aForbidden.beginLine, aForbidden.endLine,
                rInf.HasForbiddenChars(), bAllowHanging, false );

        // !!! We must have a local copy of the locale, because inside
        // getLineBreak the LinguEventListener can trigger a new formatting,
        // which can corrupt the locale pointer inside pBreakIt.
        const lang::Locale aLocale = g_pBreakIt->GetLocale( aLang );

        // determines first possible line break from nCutPos to
        // start index of current line
        LineBreakResults aResult = g_pBreakIt->GetBreakIter()->getLineBreak(
            rInf.GetText(), sal_Int32(m_nCutPos), aLocale,
            sal_Int32(rInf.GetLineStart()), aHyphOpt, aUserOpt );

        m_nBreakPos = TextFrameIndex(aResult.breakIndex);

        // if we are formatting multi portions we want to allow line breaks
        // at the border between single line and multi line portion
        // we have to be careful with footnote portions, they always come in
        // with an index 0
        if ( m_nBreakPos < rInf.GetLineStart() && rInf.IsFirstMulti() &&
             ! rInf.IsFootnoteInside() )
            m_nBreakPos = rInf.GetLineStart();

        m_nBreakStart = m_nBreakPos;

        bHyph = BreakType::HYPHENATION == aResult.breakType;
        if (bHyph)
        {
            LanguageType aNoHyphLang;
            if (rPor.InFieldGrp())
            {
                // If we are inside a field portion, we use a temporary string which
                // differs from the string at the textnode. Therefore we are not allowed
                // to call the GetLangOfChar function.
                aNoHyphLang = LANGUAGE_DONTKNOW;
            }
            else
            {
                // allow hyphenation of the word only if it's not disabled by character formatting
                aNoHyphLang = rInf.GetTextFrame()->GetLangOfChar(
                                  TextFrameIndex( sal_Int32(m_nBreakPos) +
                                            aResult.rHyphenatedWord->getHyphenationPos() ),
                                    1, true, /*bNoneIfNoHyphenation=*/true );
            }
            // allow hyphenation of the word only if it's not disabled by character formatting
            bHyph = aNoHyphLang != LANGUAGE_NONE;
        }

        if (bHyph && m_nBreakPos != TextFrameIndex(COMPLETE_STRING))
        {
            // found hyphenation position within line
            // nBreakPos is set to the hyphenation position
            m_xHyphWord = aResult.rHyphenatedWord;
            m_nBreakPos += TextFrameIndex(m_xHyphWord->getHyphenationPos() + 1);

            // if not in interactive mode, we have to break behind a soft hyphen
            if ( ! rInf.IsInterHyph() && rInf.GetIdx() )
            {
                sal_Int32 const nSoftHyphPos =
                        m_xHyphWord->getWord().indexOf( CHAR_SOFTHYPHEN );

                if ( nSoftHyphPos >= 0 &&
                     m_nBreakStart + TextFrameIndex(nSoftHyphPos) <= m_nBreakPos &&
                     m_nBreakPos > rInf.GetLineStart() )
                    m_nBreakPos = rInf.GetIdx() - TextFrameIndex(1);
            }

            if( m_nBreakPos >= rInf.GetIdx() )
            {
                nPorLen = m_nBreakPos - rInf.GetIdx();
                if ('-' == rInf.GetText()[ sal_Int32(m_nBreakPos) - 1 ])
                    m_xHyphWord = nullptr;
            }
        }
        else if ( !bHyph && m_nBreakPos >= rInf.GetLineStart() )
        {
            OSL_ENSURE(sal_Int32(m_nBreakPos) != COMPLETE_STRING, "we should have found a break pos");

            // found break position within line
            m_xHyphWord = nullptr;

            // check, if break position is soft hyphen and an underflow
            // has to be triggered
            if( m_nBreakPos > rInf.GetLineStart() && rInf.GetIdx() &&
                CHAR_SOFTHYPHEN == rInf.GetText()[ sal_Int32(m_nBreakPos) - 1 ])
            {
                m_nBreakPos = rInf.GetIdx() - TextFrameIndex(1);
            }

            if( aAdjust != SvxAdjust::Left )
            {
                // Delete any blanks at the end of a line, but be careful:
                // If a field has been expanded, we do not want to delete any
                // blanks inside the field portion. This would cause an unwanted
                // underflow
                TextFrameIndex nX = m_nBreakPos;
                while( nX > rInf.GetLineStart() &&
                       ( CH_TXTATR_BREAKWORD != cFieldChr || nX > rInf.GetIdx() ) &&
                       ( CH_BLANK == rInf.GetChar( --nX ) ||
                         CH_SIX_PER_EM == rInf.GetChar( nX ) ||
                         CH_FULL_BLANK == rInf.GetChar( nX ) ) )
                    m_nBreakPos = nX;
            }
            if( m_nBreakPos > rInf.GetIdx() )
                nPorLen = m_nBreakPos - rInf.GetIdx();
        }
        else
        {
            // no line break found, setting nBreakPos to COMPLETE_STRING
            // causes a break cut
            m_nBreakPos = TextFrameIndex(COMPLETE_STRING);
            OSL_ENSURE( m_nCutPos >= rInf.GetIdx(), "Deep cut" );
            nPorLen = m_nCutPos - rInf.GetIdx();
        }

        if (m_nBreakPos > m_nCutPos && m_nBreakPos != TextFrameIndex(COMPLETE_STRING))
        {
            const TextFrameIndex nHangingLen = m_nBreakPos - m_nCutPos;
            SwPositiveSize aTmpSize = rInf.GetTextSize( &rSI, m_nCutPos, nHangingLen );
            aTmpSize.Width(aTmpSize.Width() + nLeftRightBorderSpace);
            OSL_ENSURE( !m_pHanging, "A hanging portion is hanging around" );
            m_pHanging.reset( new SwHangingPortion( std::move(aTmpSize) ) );
            m_pHanging->SetLen( nHangingLen );
            nPorLen = m_nCutPos - rInf.GetIdx();
        }

        // If we expanded a field, we must repair the original string.
        // In case we do not trigger an underflow, we correct the nBreakPos
        // value, but we cannot correct the nBreakStart value:
        // If we have found a hyphenation position, nBreakStart can lie before
        // the field.
        if ( CH_TXTATR_BREAKWORD == cFieldChr )
        {
            if ( m_nBreakPos < rInf.GetIdx() )
                m_nBreakPos = nOldIdx - TextFrameIndex(1);
            else if (TextFrameIndex(COMPLETE_STRING) != m_nBreakPos)
            {
                OSL_ENSURE( m_nBreakPos >= m_nFieldDiff, "I've got field trouble!" );
                m_nBreakPos = m_nBreakPos - m_nFieldDiff;
            }

            OSL_ENSURE( m_nCutPos >= rInf.GetIdx() && m_nCutPos >= m_nFieldDiff,
                    "I've got field trouble, part2!" );
            m_nCutPos = m_nCutPos - m_nFieldDiff;

            OUString& rOldText = const_cast<OUString&> (rInf.GetText());
            OUString aReplacement( cFieldChr );
            rOldText = rOldText.replaceAt(sal_Int32(nOldIdx) - 1, sal_Int32(m_nFieldDiff) + 1, aReplacement);
            rInf.SetIdx( nOldIdx );

#if OSL_DEBUG_LEVEL > 0
            OSL_ENSURE( aDebugString == rInf.GetText(),
                    "Somebody, somebody, somebody put something in my string" );
#endif
        }
    }

    if( nPorLen )
    {
        rInf.GetTextSize(&rSI, rInf.GetIdx(), nPorLen, std::nullopt, nMaxComp, m_nBreakWidth,
                         nMaxSizeDiff, nExtraAscent, nExtraDescent, rInf.GetCachedVclData().get());

        rInf.SetBreakWidth(m_nBreakWidth);
        // save maximum width for later use
        if ( nMaxSizeDiff )
            rInf.SetMaxWidthDiff( &rPor, nMaxSizeDiff );

        rInf.SetExtraAscent(nExtraAscent);
        rInf.SetExtraDescent(nExtraDescent);

        m_nBreakWidth += nItalic + nLeftRightBorderSpace;
    }
    else
        m_nBreakWidth = 0;

    if (m_nBreakStart > rInf.GetIdx() + nPorLen + m_nFieldDiff)
    {
        rInf.GetTextSize(&rSI, rInf.GetIdx() + nPorLen,
                         m_nBreakStart - rInf.GetIdx() - nPorLen - m_nFieldDiff, std::nullopt,
                         nMaxComp, m_nExtraBlankWidth, nMaxSizeDiff, nExtraAscent, nExtraDescent,
                         rInf.GetCachedVclData().get());
    }

    if( m_pHanging )
    {
        m_nBreakPos = m_nCutPos;
        // Keep following SwBreakPortion in the same line.
        if ( CH_BREAK == rInf.GetChar( m_nBreakPos + m_pHanging->GetLen() ) )
            return true;
    }

    return false;
}

// returns true if word at position nPos has a different spelling
// if hyphenated at this position (old german spelling)
bool SwTextGuess::AlternativeSpelling( const SwTextFormatInfo &rInf,
    const TextFrameIndex nPos)
{
    // get word boundaries
    Boundary aBound = g_pBreakIt->GetBreakIter()->getWordBoundary(
        rInf.GetText(), sal_Int32(nPos),
        g_pBreakIt->GetLocale( rInf.GetFont()->GetLanguage() ),
        WordType::DICTIONARY_WORD, true );
    m_nBreakStart = TextFrameIndex(aBound.startPos);
    sal_Int32 nWordLen = aBound.endPos - sal_Int32(m_nBreakStart);

    // if everything else fails, we want to cut at nPos
    m_nCutPos = nPos;

    OUString const aText( rInf.GetText().copy(sal_Int32(m_nBreakStart), nWordLen) );

    // check, if word has alternative spelling
    Reference< XHyphenator >  xHyph( ::GetHyphenator() );
    OSL_ENSURE( xHyph.is(), "Hyphenator is missing");
    //! subtract 1 since the UNO-interface is 0 based
    m_xHyphWord = xHyph->queryAlternativeSpelling( aText,
                        g_pBreakIt->GetLocale( rInf.GetFont()->GetLanguage() ),
                    sal::static_int_cast<sal_Int16>(sal_Int32(nPos - m_nBreakStart)),
                    rInf.GetHyphValues() );
    return m_xHyphWord.is() && m_xHyphWord->isAlternativeSpelling();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
