/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "sdmodeltestbase.hxx"

#include <LibreOfficeKit/LibreOfficeKitEnums.h>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/XDrawView.hpp>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XMasterPageTarget.hpp>
#include <com/sun/star/table/XMergeableCell.hpp>
#include <com/sun/star/text/WritingMode2.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>

#include <comphelper/propertysequence.hxx>
#include <editeng/adjustitem.hxx>
#include <editeng/editobj.hxx>
#include <editeng/eeitem.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/request.hxx>
#include <sfx2/viewfrm.hxx>
#include <svl/intitem.hxx>
#include <svl/srchitem.hxx>
#include <svx/svxids.hrc>
#include <svx/svdoashp.hxx>
#include <svx/svdotable.hxx>
#include <svx/xlineit0.hxx>
#include <svx/xfillit0.hxx>
#include <svx/xflclit.hxx>
#include <svx/xflgrit.hxx>
#include <svx/xlndsit.hxx>
#include <SlideSorterViewShell.hxx>
#include <SlideSorter.hxx>
#include <controller/SlsClipboard.hxx>
#include <controller/SlideSorterController.hxx>
#include <controller/SlsPageSelector.hxx>
#include <svl/stritem.hxx>
#include <undo/undomanager.hxx>
#include <vcl/scheduler.hxx>
#include <docmodel/uno/UnoComplexColor.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <docmodel/uno/UnoTheme.hxx>
#include <docmodel/theme/Theme.hxx>
#include <docmodel/color/ComplexColorJSON.hxx>

#include <drawdoc.hxx>
#include <DrawDocShell.hxx>
#include <ViewShell.hxx>
#include <app.hrc>
#include <sdpage.hxx>
#include <unomodel.hxx>
#include <osl/thread.hxx>
#include <slideshow.hxx>
#include <sdresid.hxx>
#include <strings.hrc>

using namespace ::com::sun::star;

/// Impress UI tests.
class SdUiImpressTest : public SdModelTestBase
{
public:
    SdUiImpressTest()
        : SdModelTestBase(u"/sd/qa/unit/data/"_ustr)
    {
    }

    void checkCurrentPageNumber(sal_uInt16 nNum);
    void typeString(SdXImpressDocument* rImpressDocument, std::u16string_view rStr);
    void typeKey(SdXImpressDocument* rImpressDocument, const sal_uInt16 nKey);
    void insertStringToObject(sal_uInt16 nObj, std::u16string_view rStr, bool bUseEscape);
    sd::slidesorter::SlideSorterViewShell* getSlideSorterViewShell();
    void lcl_search(const OUString& rKey, bool bFindAll = false, bool bBackwards = false);
};

void SdUiImpressTest::checkCurrentPageNumber(sal_uInt16 nNum)
{
    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawView> xDrawView(xModel->getCurrentController(), uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xPage(xDrawView->getCurrentPage(), uno::UNO_SET_THROW);
    uno::Reference<beans::XPropertySet> xPropertySet(xPage, uno::UNO_QUERY);

    sal_uInt16 nPageNumber;
    xPropertySet->getPropertyValue(u"Number"_ustr) >>= nPageNumber;
    CPPUNIT_ASSERT_EQUAL(nNum, nPageNumber);
}

void SdUiImpressTest::typeKey(SdXImpressDocument* rImpressDocument, const sal_uInt16 nKey)
{
    rImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, nKey);
    rImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, nKey);
    Scheduler::ProcessEventsToIdle();
}

void SdUiImpressTest::typeString(SdXImpressDocument* rImpressDocument, std::u16string_view rStr)
{
    for (const char16_t c : rStr)
    {
        rImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, c, 0);
        rImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, c, 0);
        Scheduler::ProcessEventsToIdle();
    }
}

void SdUiImpressTest::insertStringToObject(sal_uInt16 nObj, std::u16string_view rStr,
                                           bool bUseEscape)
{
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pPage = pViewShell->GetActualPage();
    SdrObject* pShape = pPage->GetObj(nObj);
    CPPUNIT_ASSERT_MESSAGE("No Shape", pShape);
    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pShape, pView->GetSdrPageView());
    Scheduler::ProcessEventsToIdle();

    CPPUNIT_ASSERT(!pView->IsTextEdit());

    typeString(pImpressDocument, rStr);

    CPPUNIT_ASSERT(pView->IsTextEdit());

    if (bUseEscape)
    {
        typeKey(pImpressDocument, KEY_ESCAPE);
        CPPUNIT_ASSERT(!pView->IsTextEdit());
    }
}

sd::slidesorter::SlideSorterViewShell* SdUiImpressTest::getSlideSorterViewShell()
{
    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    sd::slidesorter::SlideSorterViewShell* pSSVS = nullptr;
    // Same as in sd/qa/unit/misc-tests.cxx
    for (int i = 0; i < 1000; i++)
    {
        // Process all Tasks - slide sorter is created here
        Scheduler::ProcessEventsToIdle();
        if ((pSSVS = sd::slidesorter::SlideSorterViewShell::GetSlideSorter(
                 pViewShell->GetViewShellBase()))
            != nullptr)
            break;
        osl::Thread::wait(std::chrono::milliseconds(100));
    }
    CPPUNIT_ASSERT(pSSVS);
    return pSSVS;
}

void SdUiImpressTest::lcl_search(const OUString& rKey, bool bFindAll, bool bBackwards)
{
    Scheduler::ProcessEventsToIdle();
    SvxSearchCmd eSearch = bFindAll ? SvxSearchCmd::FIND_ALL : SvxSearchCmd::FIND;

    uno::Sequence<beans::PropertyValue> aPropertyValues(comphelper::InitPropertySequence({
        { "SearchItem.SearchString", uno::Any(rKey) },
        { "SearchItem.Backward", uno::Any(bBackwards) },
        { "SearchItem.Command", uno::Any(sal_uInt16(eSearch)) },
    }));

    dispatchCommand(mxComponent, u".uno:ExecuteSearch"_ustr, aPropertyValues);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testDocumentStructureTransformExtractSlide)
{
    createSdImpressDoc("odp/tdf161430.odp");

    OString aJson = R"json(
{
    "Transforms": {
        "SlideCommands": [
            {"JumpToSlideByName": "Slide 3"},
            {"MoveSlide": 0},
            {"RenameSlide": "Slide3-Renamed"},
            {"DeleteSlide": 2},
            {"JumpToSlide": 2},
            {"DeleteSlide": ""},
            {"JumpToSlide": 1},
            {"DuplicateSlide": ""},
            {"RenameSlide": "Slide1-Duplicated"},
            {"InsertMasterSlide": 1},
            {"RenameSlide": "SlideInserted-1"},
            {"ChangeLayout": 18},
            {"JumpToSlide": "last"},
            {"InsertMasterSlideByName": "Topic Separator white"},
            {"RenameSlide": "SlideInserted-Name"},
            {"ChangeLayoutByName": "AUTOLAYOUT_TITLE_2CONTENT"},
            {"SetText.0": "first"},
            {"SetText.1": "second"},
            {"SetText.2": "third"},
            {"DuplicateSlide": 1},
            {"MoveSlide.2": 6}
        ]
    }
}
)json"_ostr;

    //transform
    uno::Sequence<css::beans::PropertyValue> aArgs = {
        comphelper::makePropertyValue(u"DataJson"_ustr,
                                      uno::Any(OStringToOUString(aJson, RTL_TEXTENCODING_UTF8))),
    };
    dispatchCommand(mxComponent, u".uno:TransformDocumentStructure"_ustr, aArgs);

    //extract
    tools::JsonWriter aJsonWriter;
    std::string_view aCommand(".uno:ExtractDocumentStructure?filter=slides");
    auto pXPresDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    pXPresDocument->getCommandValues(aJsonWriter, aCommand);

    OString aExpectedStr
        = "{ \"DocStructure\": { \"SlideCount\": 7, \"MasterSlideCount\": 8, \"MasterSlides\": { "
          "\"MasterSlide 0\": { \"Name\": \"Topic_Separator_Purple\"}, \"MasterSlide 1\": { "
          "\"Name\": \"Content_sidebar_White\"}, \"MasterSlide 2\": { \"Name\": \"Topic Separator "
          "white\"}, \"MasterSlide 3\": { \"Name\": \"Content_sidebar_White_\"}, \"MasterSlide "
          "4\": { \"Name\": \"Topic_Separator_Purple_\"}, \"MasterSlide 5\": { \"Name\": "
          "\"Content_White_Purple_Sidebar\"}, \"MasterSlide 6\": { \"Name\": \"Default 1\"}, "
          "\"MasterSlide 7\": { \"Name\": \"Default 1_\"}}, \"Slides\": { \"Slide 0\": { "
          "\"SlideName\": \"Slide3-Renamed\", \"MasterSlideName\": "
          "\"Content_White_Purple_Sidebar\", \"LayoutId\": 3, \"LayoutName\": "
          "\"AUTOLAYOUT_TITLE_2CONTENT\", \"ObjectCount\": 4, \"Objects\": { \"Objects 0\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 1, \"Paragraphs\": [ "
          "\"Friendly Open Source Project\"]}}}, \"Objects 1\": { }, \"Objects 2\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 9, \"Paragraphs\": [ \"Real "
          "Open Source\", \"100% open-source code\", \"Built with LibreOffice technology\", "
          "\"Built with Free Software technology stacks: primarily C++\", \"Runs best on Linux\", "
          "\"Open Development\", \"Anyone can contribute & participate\", \"Follow commits and "
          "tickets\", \"Public community calls - forum has details\"]}}}, \"Objects 3\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 5, \"Paragraphs\": [ "
          "\"Focus:\", \"a non-renewable resource.\", \"Office Productivity & Documents\", "
          "\"Excited about migrating your\\u0001documents\", \"Grateful to our partners for "
          "solving\\u0001other problems.\"]}}}}}, \"Slide 1\": { \"SlideName\": \"Slide 2\", "
          "\"MasterSlideName\": \"Topic_Separator_Purple\", \"LayoutId\": 3, \"LayoutName\": "
          "\"AUTOLAYOUT_TITLE_2CONTENT\", \"ObjectCount\": 1, \"Objects\": { \"Objects 0\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 3, \"Paragraphs\": [ "
          "\"Collabora Online\", \"\", \"Powerful Online Collaboration\"]}}}}}, \"Slide 2\": { "
          "\"SlideName\": \"Slide1-Duplicated\", \"MasterSlideName\": \"Topic_Separator_Purple\", "
          "\"LayoutId\": 3, \"LayoutName\": \"AUTOLAYOUT_TITLE_2CONTENT\", \"ObjectCount\": 1, "
          "\"Objects\": { \"Objects 0\": { \"TextCount\": 1, \"Texts\": { \"Text 0\": { "
          "\"ParaCount\": 3, \"Paragraphs\": [ \"Collabora Online\", \"\", \"Powerful Online "
          "Collaboration\"]}}}}}, \"Slide 3\": { \"SlideName\": \"SlideInserted-1\", "
          "\"MasterSlideName\": \"Content_sidebar_White\", \"LayoutId\": 18, \"LayoutName\": "
          "\"AUTOLAYOUT_TITLE_4CONTENT\", \"ObjectCount\": 5, \"Objects\": { \"Objects 0\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 1, \"Paragraphs\": [ "
          "\"Click to add Title\"]}}}, \"Objects 1\": { \"TextCount\": 1, \"Texts\": { \"Text 0\": "
          "{ \"ParaCount\": 1, \"Paragraphs\": [ \"Click to add Text\"]}}}, \"Objects 2\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 1, \"Paragraphs\": [ "
          "\"Click to add Text\"]}}}, \"Objects 3\": { \"TextCount\": 1, \"Texts\": { \"Text 0\": "
          "{ \"ParaCount\": 1, \"Paragraphs\": [ \"Click to add Text\"]}}}, \"Objects 4\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 1, \"Paragraphs\": [ "
          "\"Click to add Text\"]}}}}}, \"Slide 4\": { \"SlideName\": \"Slide 5\", "
          "\"MasterSlideName\": \"Topic_Separator_Purple\", \"LayoutId\": 3, \"LayoutName\": "
          "\"AUTOLAYOUT_TITLE_2CONTENT\", \"ObjectCount\": 1, \"Objects\": { \"Objects 0\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 1, \"Paragraphs\": [ \"With "
          "thanks to our Partners, Customers & Community !\"]}}}}}, \"Slide 5\": { \"SlideName\": "
          "\"SlideInserted-Name\", \"MasterSlideName\": \"Topic Separator white\", \"LayoutId\": "
          "3, \"LayoutName\": \"AUTOLAYOUT_TITLE_2CONTENT\", \"ObjectCount\": 3, \"Objects\": { "
          "\"Objects 0\": { \"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 1, "
          "\"Paragraphs\": [ \"first\"]}}}, \"Objects 1\": { \"TextCount\": 1, \"Texts\": { \"Text "
          "0\": { \"ParaCount\": 1, \"Paragraphs\": [ \"second\"]}}}, \"Objects 2\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 1, \"Paragraphs\": [ "
          "\"third\"]}}}}}, \"Slide 6\": { \"SlideName\": \"Slide 7\", \"MasterSlideName\": "
          "\"Topic_Separator_Purple\", \"LayoutId\": 3, \"LayoutName\": "
          "\"AUTOLAYOUT_TITLE_2CONTENT\", \"ObjectCount\": 1, \"Objects\": { \"Objects 0\": { "
          "\"TextCount\": 1, \"Texts\": { \"Text 0\": { \"ParaCount\": 3, \"Paragraphs\": [ "
          "\"Collabora Online\", \"\", \"Powerful Online Collaboration\"]}}}}}}}}"_ostr;

    CPPUNIT_ASSERT_EQUAL(aExpectedStr, aJsonWriter.finishAndGetAsOString());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf111522)
{
    // Load the document and create two new windows.
    createSdImpressDoc("tdf111522.odp");
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pImpressDocument->GetDocShell()->GetViewShell();
    pViewShell->GetViewFrame()->GetDispatcher()->Execute(SID_NEWWINDOW, SfxCallMode::SYNCHRON);
    sd::ViewShell* pViewShell1 = pImpressDocument->GetDocShell()->GetViewShell();
    pViewShell->GetViewFrame()->GetDispatcher()->Execute(SID_NEWWINDOW, SfxCallMode::SYNCHRON);
    sd::ViewShell* pViewShell2 = pImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell1 != pViewShell2);

    // Have slide 1 in window 1, slide 2 in window 2.
    SfxRequest aRequest(*pViewShell2->GetViewFrame(), SID_SWITCHPAGE);
    aRequest.AppendItem(SfxUInt32Item(ID_VAL_WHATPAGE, 1));
    aRequest.AppendItem(
        SfxUInt32Item(ID_VAL_WHATKIND, static_cast<sal_uInt32>(PageKind::Standard)));
    pViewShell2->ExecuteSlot(aRequest, /*bAsync=*/false);

    // Start text edit in window 1.
    SdPage* pPage1 = pViewShell1->GetActualPage();
    SdrObject* pShape1 = pPage1->GetObj(0);
    SdrView* pView1 = pViewShell1->GetView();
    pView1->MarkObj(pShape1, pView1->GetSdrPageView());
    pView1->SdrBeginTextEdit(pShape1);
    CPPUNIT_ASSERT(pView1->IsTextEdit());

    // Move the shape in window 2 & undo.
    SdPage* pPage2 = pViewShell2->GetActualPage();
    CPPUNIT_ASSERT(pPage1 != pPage2);
    SdrObject* pShape2 = pPage2->GetObj(0);
    SdrView* pView2 = pViewShell2->GetView();
    pView2->MarkObj(pShape2, pView2->GetSdrPageView());
    pView2->MoveMarkedObj(Size(1, 1), /*bCopy=*/false);
    // Without the accompanying fix in place, this test would have failed with an assertion failure
    // in SdrObjEditView::SdrEndTextEdit() as mpOldTextEditUndoManager was not nullptr.
    pViewShell2->GetViewFrame()->GetDispatcher()->Execute(SID_UNDO, SfxCallMode::SYNCHRON);

    // Rotate the shape in window 2 & undo.
    pView2->MarkObj(pShape2, pView2->GetSdrPageView());
    pView2->RotateMarkedObj(pShape2->GetLastBoundRect().Center(), /*nAngle=*/45_deg100);
    // Without the accompanying fix in place, this test would have failed with an assertion failure
    // in SdrObjEditView::SdrEndTextEdit() as mpOldTextEditUndoManager was not nullptr.
    pViewShell2->GetViewFrame()->GetDispatcher()->Execute(SID_UNDO, SfxCallMode::SYNCHRON);

    // Start text edit in window 2.
    // tdf#125824
    pView2->MarkObj(pShape2, pView2->GetSdrPageView());
    pView2->SdrBeginTextEdit(pShape2);
    CPPUNIT_ASSERT(pView2->IsTextEdit());
    // Write 'test' inside the shape
    SfxStringItem aInputString(SID_ATTR_CHAR, u"test"_ustr);
    pViewShell2->GetViewFrame()->GetDispatcher()->ExecuteList(SID_ATTR_CHAR, SfxCallMode::SYNCHRON,
                                                              { &aInputString });
    CPPUNIT_ASSERT(pView2->GetTextEditObject());
    EditView& rEditView = pView2->GetTextEditOutlinerView()->GetEditView();
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(4), rEditView.GetSelection().start.nIndex);
    pView2->SdrEndTextEdit();
    // Without the accompanying fix in place, this test would have failed with an assertion failure
    // in SdrObjEditView::SdrEndTextEdit() as mpOldTextEditUndoManager was not nullptr.
    pViewShell2->GetViewFrame()->GetDispatcher()->Execute(SID_UNDO, SfxCallMode::SYNCHRON);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf126197)
{
    // Load the document and create two new windows.
    createSdImpressDoc("tdf126197.odp");
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pImpressDocument->GetDocShell()->GetViewShell();
    pViewShell->GetViewFrame()->GetDispatcher()->Execute(SID_NEWWINDOW, SfxCallMode::SYNCHRON);
    sd::ViewShell* pViewShell1 = pImpressDocument->GetDocShell()->GetViewShell();
    pViewShell->GetViewFrame()->GetDispatcher()->Execute(SID_NEWWINDOW, SfxCallMode::SYNCHRON);
    sd::ViewShell* pViewShell2 = pImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell1 != pViewShell2);

    // Start text edit in window 1.
    SdPage* pPage1 = pViewShell1->GetActualPage();
    SdrObject* pShape1 = pPage1->GetObj(0);
    SdrView* pView1 = pViewShell1->GetView();
    pView1->MarkObj(pShape1, pView1->GetSdrPageView());
    pView1->SdrBeginTextEdit(pShape1);
    CPPUNIT_ASSERT(pView1->IsTextEdit());

    SdPage* pPage2 = pViewShell2->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(pPage1, pPage2);
    SdrObject* pShape2 = pPage2->GetObj(0);
    CPPUNIT_ASSERT_EQUAL(pShape1, pShape2);
    SdrView* pView2 = pViewShell2->GetView();
    pView2->MarkObj(pShape2, pView2->GetSdrPageView());

    // Without the accompanying fix in place, this test would have failed with an assertion failure
    // in SdrObjEditView::SdrEndTextEdit()
    pViewShell2->GetViewFrame()->GetDispatcher()->Execute(SID_DELETE, SfxCallMode::SYNCHRON);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf124708)
{
    createSdImpressDoc("tdf124708.ppt");

    dispatchCommand(mxComponent, u".uno:NextPage"_ustr, {});

    checkCurrentPageNumber(2);

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(16), pActualPage->GetObjCount());

    dispatchCommand(mxComponent, u".uno:SelectAll"_ustr, {});

    // Without the fix in place, this test would have crashed here
    dispatchCommand(mxComponent, u".uno:Delete"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), pActualPage->GetObjCount());

    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(16), pActualPage->GetObjCount());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf159666)
{
    createSdDrawDoc("tdf159666.odg");

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(12), pActualPage->GetObjCount());

    // Without the fix in place, this test would have crashed here
    dispatchCommand(mxComponent, u".uno:SelectAll"_ustr, {});

    dispatchCommand(mxComponent, u".uno:Delete"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), pActualPage->GetObjCount());

    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(12), pActualPage->GetObjCount());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf143412)
{
    createSdImpressDoc();

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();

    SdPage* pActualPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), pActualPage->GetObjCount());

    OUString aImageURL = createFileURL(u"tdf143412.svg");
    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence({
        { "FileName", uno::Any(aImageURL) },
    }));
    dispatchCommand(mxComponent, u".uno:InsertGraphic"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObjCount());

    // Without the fix in place, this test would have crashed
    // Check that converting an image to the different options doesn't crash

    dispatchCommand(mxComponent, u".uno:ChangeBezier"_ustr, {});

    dispatchCommand(mxComponent, u".uno:ChangePolygon"_ustr, {});

    dispatchCommand(mxComponent, u".uno:convert_to_contour"_ustr, {});

    dispatchCommand(mxComponent, u".uno:ConvertInto3D"_ustr, {});

    dispatchCommand(mxComponent, u".uno:ConvertInto3DLatheFast"_ustr, {});

    dispatchCommand(mxComponent, u".uno:ConvertIntoBitmap"_ustr, {});

    dispatchCommand(mxComponent, u".uno:ConvertIntoMetaFile"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObjCount());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf155211_dashed_line)
{
    createSdImpressDoc();

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();

    SdPage* pActualPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), pActualPage->GetObjCount());

    OUString aImageURL = createFileURL(u"tdf155211_dashed_line.svg");
    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence({
        { "FileName", uno::Any(aImageURL) },
    }));
    dispatchCommand(mxComponent, u".uno:InsertGraphic"_ustr, aArgs);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObjCount());

    // split the (auto-selected) svg
    dispatchCommand(mxComponent, u".uno:Break"_ustr, {});

    SdrObject* pObject = pActualPage->GetObj(2);
    const XLineStyleItem& rStyleItem = pObject->GetMergedItem(XATTR_LINESTYLE);
    // tdf#115162: Without the fix in place, this test would have failed with
    // - Expected: 2 (LineStyle_DASH)
    // - Actual  : 1 (LineStyle_SOLID)
    CPPUNIT_ASSERT_EQUAL(drawing::LineStyle_DASH, rStyleItem.GetValue());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf162455)
{
    createSdImpressDoc();

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();

    SdPage* pActualPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), pActualPage->GetObjCount());

    OUString aImageURL = createFileURL(u"tdf162455.svg");
    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence({
        { "FileName", uno::Any(aImageURL) },
    }));
    dispatchCommand(mxComponent, u".uno:InsertGraphic"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObjCount());

    // split the (auto-selected) svg up

    dispatchCommand(mxComponent, u".uno:Break"_ustr, {});

    // Get the newly created shape that has the text '100' in it
    uno::Reference<beans::XPropertySet> xShape(getShapeFromPage(4, 0));
    uno::Reference<text::XTextRange> xParagraph1(getParagraphFromShape(0, xShape));
    uno::Reference<text::XTextRange> xRun(getRunFromParagraph(0, xParagraph1));
    CPPUNIT_ASSERT_EQUAL(u"100"_ustr, xRun->getString());

    uno::Reference<beans::XPropertySet> xPropSet1(xRun, uno::UNO_QUERY);
    double fFontSize1 = xPropSet1->getPropertyValue(u"CharHeight"_ustr).get<double>();

    /* before this fix the font sizes were way too small
      - Expected: 7.5
      - Actual  : 0.300000011920929 */
    CPPUNIT_ASSERT_DOUBLES_EQUAL(7.5, fFontSize1, 0.01);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf96206)
{
    // Copying/pasting slide referring to a non-default master with a text duplicated the master

    createSdImpressDoc("odp/tdf96206.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();
    const sal_uInt16 nMasterPageCnt1 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(2), nMasterPageCnt1);
    rSSController.GetClipboard().DoCopy();
    rSSController.GetClipboard().DoPaste();
    const sal_uInt16 nMasterPageCnt2 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(nMasterPageCnt1, nMasterPageCnt2);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testDocumentCut)
{
    // Test cutting slides and verifying the document state afterwards
    createSdImpressDoc("odp/tdf96206.odp");

    // Get the slide sorter and controller
    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    // Get document and initial page count
    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();

    // Get initial page count
    const sal_uInt16 nInitialPageCount = pDoc->GetSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT(nInitialPageCount > 0);

    // Select all slides
    rSSController.GetPageSelector().SelectAllPages();

    // Check that slides are selected
    CPPUNIT_ASSERT(rSSController.GetPageSelector().GetSelectedPageCount() > 0);

    // Cut the selected slides
    rSSController.GetClipboard().DoCut();

    // Paste the cut slides
    rSSController.GetClipboard().DoPaste();

    // After pasting, we should have at least the initial number of pages
    const sal_uInt16 nPageCountAfterPaste = pDoc->GetSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT(nPageCountAfterPaste >= nInitialPageCount);

    // Verify that master page count remains unchanged throughout the operation
    const sal_uInt16 nMasterPageCount = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(2), nMasterPageCount);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf96708)
{
    createSdImpressDoc("odp/tdf96708.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();
    auto& rPageSelector = rSSController.GetPageSelector();

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();
    const sal_uInt16 nMasterPageCnt1 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(4), nMasterPageCnt1);
    rSSController.GetClipboard().DoCopy();
    rPageSelector.SelectAllPages();

    // Now wait for timers to trigger creation of auto-layout
    osl::Thread::wait(std::chrono::milliseconds(100));
    Scheduler::ProcessEventsToIdle();

    rSSController.GetClipboard().DoPaste();

    const sal_uInt16 nMasterPageCnt2 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    //FIXME: tdf#151802: Number of master pages should be 4, it's 5 instead
    //CPPUNIT_ASSERT_EQUAL(nMasterPageCnt1, nMasterPageCnt2);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(5), nMasterPageCnt2);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_default_master)
{
    // Copying/pasting slide referring to a master page.
    createSdImpressDoc("odp/tdf96206.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();
    const sal_uInt16 nMasterPageCnt1 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(2), nMasterPageCnt1);
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);
    const sal_uInt16 nMasterPageCnt2 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(nMasterPageCnt1 + 1), nMasterPageCnt2);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617)
{
    // Copying/pasting slide referring to a master page.
    createSdImpressDoc("odp/tdf96708.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();
    const sal_uInt16 nMasterPageCnt1 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(4), nMasterPageCnt1);
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);
    const sal_uInt16 nMasterPageCnt2 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(nMasterPageCnt1 + 1), nMasterPageCnt2);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_test_master_name)
{
    // Copying/pasting slide referring to a master page.
    createSdImpressDoc("odp/tdf96708.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();
    // Get the master page name
    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();
    SdPage* pMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"Master0"_ustr, aMasterPageName);
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);
    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(4, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1_Master0"_ustr, aNewMasterPageName);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_test_default_master_name)
{
    // Copying/pasting slide referring to a master page.
    createSdImpressDoc();

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();
    // Get the master page name
    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();

    SdPage* pMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"Default"_ustr, aMasterPageName);

    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1_Default"_ustr, aNewMasterPageName);

    rSSController.GetClipboard().DoPaste(true);

    SdPage* pNewMasterPage2 = pDoc->GetMasterSdPage(1, PageKind::Standard);
    OUString aNewMasterPageName2 = pNewMasterPage2->GetName();
    CPPUNIT_ASSERT_EQUAL(u"2_Default"_ustr, aNewMasterPageName2);
}
CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_basic_master_name)
{
    // Basic test for copying/pasting slide referring to a master page
    createSdImpressDoc("odp/tdf96708.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();

    SdPage* pMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"Master0"_ustr, aMasterPageName);

    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(4, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1_Master0"_ustr, aNewMasterPageName);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_leading_underscore_master_name)
{
    // Test master page name with underscore at first position
    createSdImpressDoc("odp/tdf96708.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();

    SdPage* pMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    // Rename the master page with leading underscore
    pDoc->RenameLayoutTemplate(pMasterPage->GetLayoutName(), "_Master0");

    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"_Master0"_ustr, aMasterPageName);

    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(4, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1__Master0"_ustr, aNewMasterPageName);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_middle_underscore_master_name)
{
    // Test master page name with underscore in the middle
    createSdImpressDoc("odp/tdf96708.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();

    SdPage* pMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    // Rename the master page with middle underscore
    pDoc->RenameLayoutTemplate(pMasterPage->GetLayoutName(), "Master_0");

    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"Master_0"_ustr, aMasterPageName);

    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(4, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1_Master_0"_ustr, aNewMasterPageName);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_trailing_underscore_master_name)
{
    // Test master page name with underscore at the end
    createSdImpressDoc("odp/tdf96708.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();

    SdPage* pMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    // Rename the master page with trailing underscore
    pDoc->RenameLayoutTemplate(pMasterPage->GetLayoutName(), "Master0_");

    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"Master0_"_ustr, aMasterPageName);

    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(4, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1_Master0_"_ustr, aNewMasterPageName);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_space_in_master_name)
{
    // Test master page name with space in the middle
    createSdImpressDoc("odp/tdf96708.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();

    SdPage* pMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    // Rename the master page with space in the middle
    pDoc->RenameLayoutTemplate(pMasterPage->GetLayoutName(), "Master 0");

    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"Master 0"_ustr, aMasterPageName);

    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(4, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1_Master 0"_ustr, aNewMasterPageName);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_Double_Copy)
{
    // Test master page name with double copy
    createSdImpressDoc();

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();
    // Set edit master page mode
    rSSController.ChangeEditMode(EditMode::MasterPage);

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();

    // Get the master page name
    const sal_uInt16 nMasterPageCnt1 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(1), nMasterPageCnt1);
    SdPage* pMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    // Rename the master page with master name
    pDoc->RenameLayoutTemplate(pMasterPage->GetLayoutName(), "master");
    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"master"_ustr, aMasterPageName);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pMasterPage);

    // Copy and paste the master page
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    const sal_uInt16 nMasterPageCnt2 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(2), nMasterPageCnt2);

    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(1, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1_master"_ustr, aNewMasterPageName);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pMasterPage);

    // Copy again
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);
    const sal_uInt16 nMasterPageCnt3 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(3), nMasterPageCnt3);
    SdPage* pNewMasterPage2 = pDoc->GetMasterSdPage(2, PageKind::Standard);
    OUString aNewMasterPageName2 = pNewMasterPage2->GetName();
    CPPUNIT_ASSERT_EQUAL(u"2_master"_ustr, aNewMasterPageName2);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pMasterPage);

    // Copy again
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    const sal_uInt16 nMasterPageCnt4 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(4), nMasterPageCnt4);
    SdPage* pNewMasterPage3 = pDoc->GetMasterSdPage(3, PageKind::Standard);
    OUString aNewMasterPageName3 = pNewMasterPage3->GetName();
    CPPUNIT_ASSERT_EQUAL(u"3_master"_ustr, aNewMasterPageName3);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_Double_Copy_Default)
{
    // Test master page name with double copy
    createSdImpressDoc();

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();
    // Set edit master page mode
    rSSController.ChangeEditMode(EditMode::MasterPage);

    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();

    // Get the master page name
    const sal_uInt16 nMasterPageCnt1 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(1), nMasterPageCnt1);
    SdPage* pMasterPage = pDoc->GetMasterSdPage(0, PageKind::Standard);
    // Rename the master page with default name
    pDoc->RenameLayoutTemplate(pMasterPage->GetLayoutName(), "Default");
    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"Default"_ustr, aMasterPageName);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pMasterPage);

    // Copy and paste the master page
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    const sal_uInt16 nMasterPageCnt2 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(2), nMasterPageCnt2);

    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(1, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1_Default"_ustr, aNewMasterPageName);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pMasterPage);
    // Copy again
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);
    const sal_uInt16 nMasterPageCnt3 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(3), nMasterPageCnt3);
    SdPage* pNewMasterPage2 = pDoc->GetMasterSdPage(2, PageKind::Standard);
    OUString aNewMasterPageName2 = pNewMasterPage2->GetName();
    CPPUNIT_ASSERT_EQUAL(u"2_Default"_ustr, aNewMasterPageName2);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pMasterPage);
    // Copy again
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);
    const sal_uInt16 nMasterPageCnt4 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(4), nMasterPageCnt4);
    SdPage* pNewMasterPage3 = pDoc->GetMasterSdPage(3, PageKind::Standard);
    OUString aNewMasterPageName3 = pNewMasterPage3->GetName();
    CPPUNIT_ASSERT_EQUAL(u"3_Default"_ustr, aNewMasterPageName3);
}
CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf45617_Double_Copy_CopiedPage)
{
    // Test master page name with double copy for copied page which doesn't have a normal page created
    createSdImpressDoc("odp/tdf96708.odp");
    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();
    // Set edit master page mode
    rSSController.ChangeEditMode(EditMode::MasterPage);
    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();
    // Get the master page name
    const sal_uInt16 nMasterPageCnt1 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(4), nMasterPageCnt1);
    SdPage* pMasterPage = pDoc->GetMasterSdPage(3, PageKind::Standard);
    OUString aMasterPageName = pMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"Master3"_ustr, aMasterPageName);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pMasterPage);
    // Copy and paste the master page
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    const sal_uInt16 nMasterPageCnt2 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(5), nMasterPageCnt2);
    SdPage* pNewMasterPage = pDoc->GetMasterSdPage(4, PageKind::Standard);
    OUString aNewMasterPageName = pNewMasterPage->GetName();
    CPPUNIT_ASSERT_EQUAL(u"1_Master3"_ustr, aNewMasterPageName);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pNewMasterPage);
    // Copy again
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    const sal_uInt16 nMasterPageCnt3 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(6), nMasterPageCnt3);
    SdPage* pNewMasterPage2 = pDoc->GetMasterSdPage(5, PageKind::Standard);
    OUString aNewMasterPageName2 = pNewMasterPage2->GetName();
    CPPUNIT_ASSERT_EQUAL(u"2_Master3"_ustr, aNewMasterPageName2);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pNewMasterPage2);
    // Copy again
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    const sal_uInt16 nMasterPageCnt4 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(7), nMasterPageCnt4);
    SdPage* pNewMasterPage3 = pDoc->GetMasterSdPage(6, PageKind::Standard);
    OUString aNewMasterPageName3 = pNewMasterPage3->GetName();
    CPPUNIT_ASSERT_EQUAL(u"3_Master3"_ustr, aNewMasterPageName3);

    // Reslect the master page
    rSSController.GetPageSelector().DeselectAllPages();
    rSSController.GetPageSelector().SelectPage(pNewMasterPage);
    // Copy again
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    const sal_uInt16 nMasterPageCnt5 = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(8), nMasterPageCnt5);
    SdPage* pNewMasterPage4 = pDoc->GetMasterSdPage(7, PageKind::Standard);
    OUString aNewMasterPageName4 = pNewMasterPage4->GetName();
    CPPUNIT_ASSERT_EQUAL(u"4_Master3"_ustr, aNewMasterPageName4);
}
CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testUndoMergeMasterPage)
{
    // Test undo for merging master pages only
    createSdImpressDoc("odp/tdf96708.odp");

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();

    // Get document and check initial master page count
    SdXImpressDocument* pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDoc = pXImpressDocument->GetDoc();
    const sal_uInt16 nInitialMasterPageCount = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(4), nInitialMasterPageCount);

    // Copy and paste master page only
    rSSController.GetClipboard().DoCopy(true);
    rSSController.GetClipboard().DoPaste(true);

    // Check that a master page was added
    const sal_uInt16 nAfterPasteMasterPageCount = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt16>(nInitialMasterPageCount + 1),
                         nAfterPasteMasterPageCount);

    // Undo the master page merge
    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    // Check that the master page count is back to the initial value
    const sal_uInt16 nAfterUndoMasterPageCount = pDoc->GetMasterSdPageCount(PageKind::Standard);
    CPPUNIT_ASSERT_EQUAL(nInitialMasterPageCount, nAfterUndoMasterPageCount);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf139996)
{
    createSdImpressDoc();

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();
    auto& rPageSelector = rSSController.GetPageSelector();

    CPPUNIT_ASSERT_EQUAL(1, rPageSelector.GetSelectedPageCount());

    rPageSelector.DeselectAllPages();

    CPPUNIT_ASSERT_EQUAL(0, rPageSelector.GetSelectedPageCount());

    // Without the fix in place, this test would have crashed here
    dispatchCommand(mxComponent, u".uno:MovePageUp"_ustr, {});

    dispatchCommand(mxComponent, u".uno:MovePageDown"_ustr, {});

    dispatchCommand(mxComponent, u".uno:MovePageTop"_ustr, {});

    dispatchCommand(mxComponent, u".uno:MovePageBottom"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(0, rPageSelector.GetSelectedPageCount());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf128651)
{
    // Error was, that undo and redo changes size of the shape. Affected actions were e.g.
    // extrusion on/off, shadow on/off, changes on line or fill attributes.
    // All these actions do not change the snap rectangle.
    createSdImpressDoc("tdf128651_CustomShapeUndo.odp");
    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pObject = pActualPage->GetObj(0);
    auto pCustomShape = dynamic_cast<SdrObjCustomShape*>(pObject);
    CPPUNIT_ASSERT_MESSAGE("No Shape", pCustomShape);
    const sal_Int32 nOrigWidth(pCustomShape->GetSnapRect().GetWidth());

    SdDrawDocument* pDocument = pXImpressDocument->GetDoc();
    sd::UndoManager* pUndoManager = pDocument->GetUndoManager();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), pUndoManager->GetUndoActionCount());
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), pUndoManager->GetRedoActionCount());

    SdrView* pView = pViewShell->GetView();
    pView->MarkObj(pCustomShape, pView->GetSdrPageView());
    pViewShell->GetViewFrame()->GetDispatcher()->Execute(SID_EXTRUSION_TOGGLE,
                                                         SfxCallMode::SYNCHRON);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), pUndoManager->GetUndoActionCount());

    pViewShell->GetViewFrame()->GetDispatcher()->Execute(SID_UNDO, SfxCallMode::SYNCHRON);
    const sal_Int32 nUndoWidth(pCustomShape->GetSnapRect().GetWidth());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Undo changes width", nOrigWidth, nUndoWidth);

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), pUndoManager->GetRedoActionCount());
    pViewShell->GetViewFrame()->GetDispatcher()->Execute(SID_REDO, SfxCallMode::SYNCHRON);
    const sal_Int32 nRedoWidth(pCustomShape->GetSnapRect().GetWidth());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Redo changes width", nUndoWidth, nRedoWidth);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf126605)
{
    createSdImpressDoc();

    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, {});

    insertStringToObject(0, u"Test", /*bUseEscape*/ false);

    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage(xDrawPagesSupplier->getDrawPages()->getByIndex(1),
                                                 uno::UNO_QUERY);

    uno::Reference<beans::XPropertySet> xShape(xDrawPage->getByIndex(0), uno::UNO_QUERY);

    uno::Reference<text::XText> xText
        = uno::Reference<text::XTextRange>(xShape, uno::UNO_QUERY_THROW)->getText();
    CPPUNIT_ASSERT_MESSAGE("Not a text shape", xText.is());

    uno::Reference<container::XEnumerationAccess> paraEnumAccess(xText, uno::UNO_QUERY);
    uno::Reference<container::XEnumeration> paraEnum(paraEnumAccess->createEnumeration());

    // Get first paragraph
    uno::Reference<text::XTextRange> xParagraph(paraEnum->nextElement(), uno::UNO_QUERY_THROW);
    uno::Reference<beans::XPropertySet> xPropSet(xParagraph, uno::UNO_QUERY_THROW);

    sal_Int16 nWritingMode = 0;
    xPropSet->getPropertyValue(u"WritingMode"_ustr) >>= nWritingMode;
    CPPUNIT_ASSERT_EQUAL(text::WritingMode2::LR_TB, nWritingMode);

    // Without the fix in place, this test would have crashed here
    dispatchCommand(mxComponent, u".uno:ParaRightToLeft"_ustr, {});

    xPropSet->getPropertyValue(u"WritingMode"_ustr) >>= nWritingMode;
    CPPUNIT_ASSERT_EQUAL(text::WritingMode2::RL_TB, nWritingMode);

    dispatchCommand(mxComponent, u".uno:ParaLeftToRight"_ustr, {});

    xPropSet->getPropertyValue(u"WritingMode"_ustr) >>= nWritingMode;
    CPPUNIT_ASSERT_EQUAL(text::WritingMode2::LR_TB, nWritingMode);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf100950)
{
    createSdImpressDoc();

    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, {});

    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, {});

    insertStringToObject(0, u"Test", /*bUseEscape*/ true);

    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();
    auto& rSSController = pSSVS->GetSlideSorter().GetController();
    auto& rPageSelector = rSSController.GetPageSelector();

    // Without the fix in place, this test would have failed here
    CPPUNIT_ASSERT(rPageSelector.IsPageSelected(2));
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf130581_undo_hide_show_slide)
{
    createSdImpressDoc();

    // Hide slide and check the number of available undo actions
    dispatchCommand(mxComponent, u".uno:ShowSlide"_ustr, {});
    dispatchCommand(mxComponent, u".uno:HideSlide"_ustr, {});

    // There should be a single undo action, i.e., hide slide
    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    SdDrawDocument* pDocument = pXImpressDocument->GetDoc();
    sd::UndoManager* pUndoManager = pDocument->GetUndoManager();
    // Check if there is the correct undo action, i.e., hide slide
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), pUndoManager->GetUndoActionCount());
    CPPUNIT_ASSERT_EQUAL(SdResId(STR_UNDO_HIDE_SLIDE), pUndoManager->GetUndoActionComment());
    sd::slidesorter::SlideSorterViewShell* pSSVS = getSlideSorterViewShell();

    // Check if the page is actually hidden
    auto& rSSController = pSSVS->GetSlideSorter().GetController();
    auto& rPageSelector = rSSController.GetPageSelector();
    CPPUNIT_ASSERT_EQUAL(true, rPageSelector.IsPageExcluded(0));

    // Undo hide slide action and check the number of available redo actions
    // including the correct undo action, i.e., hide slide
    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), pUndoManager->GetRedoActionCount());
    CPPUNIT_ASSERT_EQUAL(SdResId(STR_UNDO_HIDE_SLIDE), pUndoManager->GetRedoActionComment());
    CPPUNIT_ASSERT_EQUAL(false, rPageSelector.IsPageExcluded(0));

    // Show slide and check the number of available undo actions
    dispatchCommand(mxComponent, u".uno:Redo"_ustr, {});
    CPPUNIT_ASSERT_EQUAL(true, rPageSelector.IsPageExcluded(0));
    dispatchCommand(mxComponent, u".uno:ShowSlide"_ustr, {});
    // There should be two undo actions, i.e., show and hide slide
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), pUndoManager->GetUndoActionCount());
    CPPUNIT_ASSERT_EQUAL(SdResId(STR_UNDO_SHOW_SLIDE), pUndoManager->GetUndoActionComment());
    CPPUNIT_ASSERT_EQUAL(false, rPageSelector.IsPageExcluded(0));

    // Undo show slide and check the number of available undo/redo actions
    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});
    // There should be one undo action, i.e., hide slide, and one redo action, i.e., show slide
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), pUndoManager->GetUndoActionCount());
    CPPUNIT_ASSERT_EQUAL(SdResId(STR_UNDO_HIDE_SLIDE), pUndoManager->GetUndoActionComment());
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), pUndoManager->GetRedoActionCount());
    CPPUNIT_ASSERT_EQUAL(SdResId(STR_UNDO_SHOW_SLIDE), pUndoManager->GetRedoActionComment());
    CPPUNIT_ASSERT_EQUAL(true, rPageSelector.IsPageExcluded(0));
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf129346)
{
    createSdImpressDoc();

    dispatchCommand(mxComponent, u".uno:DiaMode"_ustr, {});
    checkCurrentPageNumber(1);

    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, {});
    checkCurrentPageNumber(2);

    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});
    checkCurrentPageNumber(1);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testmoveSlides)
{
    createSdImpressDoc();

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();

    uno::Sequence<beans::PropertyValue> aArgs(
        comphelper::InitPropertySequence({ { "PageName", uno::Any(u"Test 1"_ustr) },
                                           { "WhatLayout", uno::Any(sal_Int32(1)) },
                                           { "IsPageBack", uno::Any(false) },
                                           { "IsPageObj", uno::Any(false) } }));

    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, aArgs);
    checkCurrentPageNumber(2);

    CPPUNIT_ASSERT_EQUAL(u"Test 1"_ustr, pViewShell->GetActualPage()->GetName());

    aArgs = comphelper::InitPropertySequence({ { "PageName", uno::Any(u"Test 2"_ustr) },
                                               { "WhatLayout", uno::Any(sal_Int32(1)) },
                                               { "IsPageBack", uno::Any(false) },
                                               { "IsPageObj", uno::Any(false) } });

    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, aArgs);
    checkCurrentPageNumber(3);

    CPPUNIT_ASSERT_EQUAL(u"Test 2"_ustr, pViewShell->GetActualPage()->GetName());

    // Move slide 'Test 2' up
    for (size_t i = 2; i > 0; --i)
    {
        dispatchCommand(mxComponent, u".uno:MovePageUp"_ustr, {});
        checkCurrentPageNumber(i);
        CPPUNIT_ASSERT_EQUAL(u"Test 2"_ustr, pViewShell->GetActualPage()->GetName());
    }

    // Move slide 'Test 2' down
    for (size_t i = 2; i < 4; ++i)
    {
        dispatchCommand(mxComponent, u".uno:MovePageDown"_ustr, {});
        checkCurrentPageNumber(i);
        CPPUNIT_ASSERT_EQUAL(u"Test 2"_ustr, pViewShell->GetActualPage()->GetName());
    }

    // Move slide 'Test 2' to the top
    dispatchCommand(mxComponent, u".uno:MovePageFirst"_ustr, {});
    checkCurrentPageNumber(1);
    CPPUNIT_ASSERT_EQUAL(u"Test 2"_ustr, pViewShell->GetActualPage()->GetName());

    // Move slide 'Test 2' to the bottom
    dispatchCommand(mxComponent, u".uno:MovePageLast"_ustr, {});
    checkCurrentPageNumber(3);
    CPPUNIT_ASSERT_EQUAL(u"Test 2"_ustr, pViewShell->GetActualPage()->GetName());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf148620)
{
    createSdImpressDoc();

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());

    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage(xDrawPagesSupplier->getDrawPages()->getByIndex(0),
                                                 uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xShape(xDrawPage->getByIndex(1), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u""_ustr, xShape->getString());

    insertStringToObject(1, u"one", /*bUseEscape*/ false);
    typeKey(pXImpressDocument, KEY_RETURN);
    typeString(pXImpressDocument, u"two");
    typeKey(pXImpressDocument, KEY_RETURN);
    typeString(pXImpressDocument, u"three");
    typeKey(pXImpressDocument, KEY_RETURN);
    typeString(pXImpressDocument, u"four");
    typeKey(pXImpressDocument, KEY_RETURN);
    typeString(pXImpressDocument, u"five");
    typeKey(pXImpressDocument, KEY_RETURN);
    typeString(pXImpressDocument, u"six");

    CPPUNIT_ASSERT_EQUAL(u"One\nTwo\nThree\nFour\nFive\nsix"_ustr, xShape->getString());

    uno::Sequence<beans::PropertyValue> aArgs(
        comphelper::InitPropertySequence({ { "KeyModifier", uno::Any(sal_Int32(0)) } }));
    dispatchCommand(mxComponent, u".uno:OutlineUp"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"One\nTwo\nThree\nFour\nsix\nFive"_ustr, xShape->getString());

    dispatchCommand(mxComponent, u".uno:OutlineUp"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"One\nTwo\nThree\nsix\nFour\nFive"_ustr, xShape->getString());

    dispatchCommand(mxComponent, u".uno:OutlineUp"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"One\nTwo\nsix\nThree\nFour\nFive"_ustr, xShape->getString());

    dispatchCommand(mxComponent, u".uno:OutlineUp"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"One\nsix\nTwo\nThree\nFour\nFive"_ustr, xShape->getString());

    dispatchCommand(mxComponent, u".uno:OutlineUp"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"six\nOne\nTwo\nThree\nFour\nFive"_ustr, xShape->getString());

    dispatchCommand(mxComponent, u".uno:OutlineDown"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"One\nsix\nTwo\nThree\nFour\nFive"_ustr, xShape->getString());

    dispatchCommand(mxComponent, u".uno:OutlineDown"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"One\nTwo\nsix\nThree\nFour\nFive"_ustr, xShape->getString());

    dispatchCommand(mxComponent, u".uno:OutlineDown"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"One\nTwo\nThree\nsix\nFour\nFive"_ustr, xShape->getString());

    dispatchCommand(mxComponent, u".uno:OutlineDown"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"One\nTwo\nThree\nFour\nsix\nFive"_ustr, xShape->getString());

    dispatchCommand(mxComponent, u".uno:OutlineDown"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(u"One\nTwo\nThree\nFour\nFive\nsix"_ustr, xShape->getString());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf141703)
{
    createSdImpressDoc();

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());

    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence(
        { { "Rows", uno::Any(sal_Int32(2)) }, { "Columns", uno::Any(sal_Int32(2)) } }));

    dispatchCommand(mxComponent, u".uno:InsertTable"_ustr, aArgs);

    // Move to A1 using Alt + Tab and write 'A'
    for (int i = 0; i < 3; i++)
    {
        pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_SHIFT | KEY_TAB);
        pXImpressDocument->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_SHIFT | KEY_TAB);
        Scheduler::ProcessEventsToIdle();
    }

    typeString(pXImpressDocument, u"A");

    // Move to A2 with Tab and write 'B'
    typeKey(pXImpressDocument, KEY_TAB);

    typeString(pXImpressDocument, u"B");

    typeKey(pXImpressDocument, KEY_ESCAPE);

    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pActualPage->GetObj(2));
    CPPUNIT_ASSERT(pTableObject);

    uno::Reference<css::table::XTable> xTable(pTableObject->getTable(), uno::UNO_SET_THROW);
    uno::Reference<css::table::XMergeableCell> xCellA1(xTable->getCellByPosition(0, 0),
                                                       uno::UNO_QUERY_THROW);
    uno::Reference<css::table::XMergeableCell> xCellA2(xTable->getCellByPosition(1, 0),
                                                       uno::UNO_QUERY_THROW);

    uno::Reference<text::XText> xTextA1
        = uno::Reference<text::XTextRange>(xCellA1, uno::UNO_QUERY_THROW)->getText();

    // Without the fix in place, this test would have failed with
    // - Expected: A
    // - Actual  :
    CPPUNIT_ASSERT_EQUAL(u"A"_ustr, xTextA1->getString());

    uno::Reference<text::XText> xTextA2
        = uno::Reference<text::XTextRange>(xCellA2, uno::UNO_QUERY_THROW)->getText();
    CPPUNIT_ASSERT_EQUAL(u"B"_ustr, xTextA2->getString());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf164855)
{
    createSdImpressDoc();

    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence(
        { { "Rows", uno::Any(sal_Int32(2)) }, { "Columns", uno::Any(sal_Int32(2)) } }));

    dispatchCommand(mxComponent, u".uno:InsertTable"_ustr, aArgs);

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObjCount());

    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pActualPage->GetObj(2));
    CPPUNIT_ASSERT(pTableObject);

    const EditTextObject& rEdit
        = pTableObject->getText(0)->GetOutlinerParaObject()->GetTextObject();
    const SfxItemSet& rParaAttribs = rEdit.GetParaAttribs(0);
    auto pAdjust = rParaAttribs.GetItem(EE_PARA_JUST);
    CPPUNIT_ASSERT_EQUAL(SvxAdjust::Left, pAdjust->GetAdjust());

    // Without the fix in place, this test would have crashed here
    dispatchCommand(mxComponent, u".uno:RightPara"_ustr, {});

    const EditTextObject& rEdit2
        = pTableObject->getText(0)->GetOutlinerParaObject()->GetTextObject();
    const SfxItemSet& rParaAttribs2 = rEdit2.GetParaAttribs(0);
    pAdjust = rParaAttribs2.GetItem(EE_PARA_JUST);
    CPPUNIT_ASSERT_EQUAL(SvxAdjust::Right, pAdjust->GetAdjust());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf127481)
{
    createSdImpressDoc();

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), pActualPage->GetObjCount());

    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence(
        { { "Rows", uno::Any(sal_Int32(1)) }, { "Columns", uno::Any(sal_Int32(1)) } }));

    dispatchCommand(mxComponent, u".uno:InsertTable"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObjCount());

    dispatchCommand(mxComponent, u".uno:DuplicatePage"_ustr, aArgs);

    checkCurrentPageNumber(2);

    pActualPage = pViewShell->GetActualPage();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObjCount());

    auto pTableObject = dynamic_cast<sdr::table::SdrTableObj*>(pActualPage->GetObj(2));
    CPPUNIT_ASSERT(pTableObject);

    //without the fix, it would crash here
    pViewShell->GetView()->SdrBeginTextEdit(pTableObject);

    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), pActualPage->GetObjCount());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testPageFillColor)
{
    // Load the document and create two new windows.
    createSdImpressDoc("tdf126197.odp");
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pImpressDocument->GetDocShell()->GetViewShell();

    // Set FillPageColor

    uno::Sequence<beans::PropertyValue> aPropertyValues = {
        comphelper::makePropertyValue(u"FillColor"_ustr, static_cast<sal_Int32>(0xff0000)),
    };
    dispatchCommand(mxComponent, u".uno:FillPageColor"_ustr, aPropertyValues);

    SdPage* pPage = pViewShell->getCurrentPage();
    const SfxItemSet& rPageAttr = pPage->getSdrPageProperties().GetItemSet();

    const XFillStyleItem* pFillStyle = rPageAttr.GetItem(XATTR_FILLSTYLE);
    drawing::FillStyle eXFS = pFillStyle->GetValue();
    CPPUNIT_ASSERT_EQUAL(drawing::FillStyle_SOLID, eXFS);

    Color aColor = rPageAttr.GetItem(XATTR_FILLCOLOR)->GetColorValue();
    CPPUNIT_ASSERT_EQUAL(COL_LIGHTRED, aColor);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testPageFillGradient)
{
    // Load the document and create two new windows.
    createSdImpressDoc("tdf126197.odp");
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pImpressDocument->GetDocShell()->GetViewShell();

    // Set FillPageColor

    uno::Sequence<beans::PropertyValue> aPropertyValues(comphelper::InitPropertySequence({
        { "FillPageGradientJSON",
          uno::Any(u"{\"style\":\"LINEAR\",\"startcolor\":\"ff0000\",\"endcolor\":\"0000ff\","
                   "\"angle\":\"300\",\"border\":\"0\",\"x\":\"0\",\"y\":\"0\",\"intensstart\":"
                   "\"100\",\"intensend\":\"100\",\"stepcount\":\"0\"}"_ustr) },
    }));

    dispatchCommand(mxComponent, u".uno:FillPageGradient"_ustr, aPropertyValues);

    SdPage* pPage = pViewShell->getCurrentPage();
    const SfxItemSet& rPageAttr = pPage->getSdrPageProperties().GetItemSet();

    const XFillStyleItem* pFillStyle = rPageAttr.GetItem(XATTR_FILLSTYLE);
    drawing::FillStyle eXFS = pFillStyle->GetValue();
    CPPUNIT_ASSERT_EQUAL(drawing::FillStyle_GRADIENT, eXFS);

    basegfx::BGradient aGradient(rPageAttr.GetItem(XATTR_FILLGRADIENT)->GetGradientValue());
    const basegfx::BColorStops& rColorStops(aGradient.GetColorStops());

    CPPUNIT_ASSERT_EQUAL(size_t(2), rColorStops.size());
    CPPUNIT_ASSERT_EQUAL(0.0, rColorStops[0].getStopOffset());
    CPPUNIT_ASSERT_EQUAL(COL_LIGHTRED, Color(rColorStops[0].getStopColor()));
    CPPUNIT_ASSERT(basegfx::fTools::equal(rColorStops[1].getStopOffset(), 1.0));
    CPPUNIT_ASSERT_EQUAL(COL_LIGHTBLUE, Color(rColorStops[1].getStopColor()));
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf134053)
{
    // Error was, that dashes and dots were longer than in MS Office.
    createSdImpressDoc("pptx/tdf134053_dashdot.pptx");
    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    SdPage* pActualPage = pViewShell->GetActualPage();
    SdrObject* pShape = pActualPage->GetObj(0);
    CPPUNIT_ASSERT_MESSAGE("No Shape", pShape);

    SdDrawDocument* pDocument = pXImpressDocument->GetDoc();
    sd::UndoManager* pUndoManager = pDocument->GetUndoManager();

    // tdf#114613: Without the fix in place, this test would have failed with
    // - Expected: 0
    // - Actual  : 8
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), pUndoManager->GetUndoActionCount());
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(0), pUndoManager->GetRedoActionCount());

    XDash dash = pShape->GetMergedItem(XATTR_LINEDASH).GetDashValue();

    // Because 0% is not possible as dash length (as of June 2020) 1% is used in the fix.
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Distance", 399.0, dash.GetDistance());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Dot length", 301.0, dash.GetDotLen());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Dash length", 1.0, dash.GetDashLen());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testSpellOnlineParameter)
{
    createSdImpressDoc();
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    bool bSet = pImpressDocument->GetDoc()->GetOnlineSpell();

    uno::Sequence<beans::PropertyValue> params(
        comphelper::InitPropertySequence({ { "Enable", uno::Any(!bSet) } }));
    dispatchCommand(mxComponent, u".uno:SpellOnline"_ustr, params);
    CPPUNIT_ASSERT_EQUAL(!bSet, pImpressDocument->GetDoc()->GetOnlineSpell());

    // set the same state as now and we don't expect any change (no-toggle)
    params = comphelper::InitPropertySequence({ { "Enable", uno::Any(!bSet) } });
    dispatchCommand(mxComponent, u".uno:SpellOnline"_ustr, params);
    CPPUNIT_ASSERT_EQUAL(!bSet, pImpressDocument->GetDoc()->GetOnlineSpell());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf38669)
{
    createSdImpressDoc();
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    CPPUNIT_ASSERT(pImpressDocument);

    // Insert shape with ctrl key
    uno::Sequence<beans::PropertyValue> aArgs(
        comphelper::InitPropertySequence({ { "KeyModifier", uno::Any(KEY_MOD1) } }));
    dispatchCommand(mxComponent, u".uno:BasicShapes.rectangle"_ustr, aArgs);

    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage(xDrawPagesSupplier->getDrawPages()->getByIndex(0),
                                                 uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xDrawPage->getCount());

    typeString(pImpressDocument, u"°");

    uno::Reference<text::XTextRange> xShape(xDrawPage->getByIndex(2), uno::UNO_QUERY);
    // Without the fix in place, this test would have failed with:
    // - Expected: °
    // - Actual  : ㅀ
    CPPUNIT_ASSERT_EQUAL(u"°"_ustr, xShape->getString());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf151417)
{
    createSdImpressDoc();
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    CPPUNIT_ASSERT(pImpressDocument);

    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xDraws = xDrawPagesSupplier->getDrawPages();
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xDraws->getCount());

    uno::Reference<drawing::XDrawPage> xDrawPage(xDrawPagesSupplier->getDrawPages()->getByIndex(0),
                                                 uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xDrawPage->getCount());

    uno::Sequence<beans::PropertyValue> aArgs(
        comphelper::InitPropertySequence({ { "KeyModifier", uno::Any(KEY_MOD1) } }));

    // Without the fix in place, this test would have crashed here
    dispatchCommand(mxComponent, u".uno:Edit"_ustr, aArgs);

    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xDrawPage->getCount());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf123841)
{
    // To check if selecting unfilled rectangle produces unfilled rectangle
    createSdImpressDoc();
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    CPPUNIT_ASSERT(pImpressDocument);

    uno::Sequence<beans::PropertyValue> aArgs(
        comphelper::InitPropertySequence({ { "KeyModifier", uno::Any(KEY_MOD1) } }));
    dispatchCommand(mxComponent, u".uno:Rect_Unfilled"_ustr, aArgs);

    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage(xDrawPagesSupplier->getDrawPages()->getByIndex(0),
                                                 uno::UNO_QUERY);

    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xDrawPage->getCount());

    for (int i = 0; i < 3; i++)
    {
        uno::Reference<beans::XPropertySet> XPropSet(xDrawPage->getByIndex(i), uno::UNO_QUERY);
        drawing::FillStyle eFillStyle = drawing::FillStyle_NONE;
        XPropSet->getPropertyValue(u"FillStyle"_ustr) >>= eFillStyle;

        // Without the fix in place, this test would have failed with
        // with drawing::FillStyle_NONE != drawing::FillStyle_SOLID
        CPPUNIT_ASSERT_EQUAL(drawing::FillStyle_NONE, eFillStyle);
    }
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testSearchAllInDocumentAndNotes)
{
    // tdf#142478
    // "find all" produces a crash when the search string exists in notes
    // and the document

    createSdImpressDoc("odp/search-all-notes.odp");

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pXImpressDocument->GetDocShell()->GetViewShell();
    CPPUNIT_ASSERT(pViewShell);

    lcl_search(u"Crash"_ustr, /*bFindAll=*/true, /*bBackwards=*/true);
}

#if !defined(_WIN32) && !defined(MACOSX)
// for some reason, the search for "second" (or "Second") didn't return page 2 in WIN and MACOS
CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf123658_SearchAfterSlideChange)
{
    createSdImpressDoc("odp/tdf123658_SearchAfterSlideChange.odp");

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());

    lcl_search(u"second"_ustr);
    checkCurrentPageNumber(2);

    pXImpressDocument->setPart(0); // Switch to 1st page

    lcl_search(u"of"_ustr);
    // Instead of finding this on the 1st page (or on the 2nd page would be acceptable too)
    // it was going to the third page.
    checkCurrentPageNumber(1);
}
#endif

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf142589)
{
    createSdImpressDoc();

    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pImpressDocument->GetDocShell()->GetViewShell();

    SfxRequest aRequest(*pViewShell->GetViewFrame(), SID_PRESENTATION);
    pImpressDocument->GetDoc()->getPresentationSettings().mbCustomShow = true;
    pImpressDocument->GetDoc()->getPresentationSettings().mbStartCustomShow = true;
    sd::slideshowhelp::ShowSlideShow(aRequest, *pImpressDocument->GetDoc());
    CPPUNIT_ASSERT_EQUAL(false, pImpressDocument->GetDoc()->getPresentationSettings().mbCustomShow);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testCharColorTheme)
{
    // Given an Impress document with a shape, with its text selected:
    createSdImpressDoc();
    uno::Reference<drawing::XDrawPagesSupplier> xPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xPage(xPagesSupplier->getDrawPages()->getByIndex(0),
                                             uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xShape(xPage->getByIndex(0), uno::UNO_QUERY);
    {
        uno::Reference<text::XSimpleText> xText = xShape->getText();
        xText->insertString(xText->getStart(), u"test"_ustr, false);
    }
    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<view::XSelectionSupplier> xController(xModel->getCurrentController(),
                                                         uno::UNO_QUERY);
    xController->select(uno::Any(xShape));
    Scheduler::ProcessEventsToIdle();
    dispatchCommand(mxComponent, u".uno:Text"_ustr, {});
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pImpressDocument->GetDocShell()->GetViewShell();
    SdrView* pView = pViewShell->GetView();
    CPPUNIT_ASSERT(pView->IsTextEdit());
    dispatchCommand(mxComponent, u".uno:SelectAll"_ustr, {});

    // When picking a theme color on the sidebar:
    {
        model::ComplexColor aComplexColor;
        aComplexColor.setThemeColor(model::ThemeColorType::Accent1);
        aComplexColor.addTransformation({ model::TransformationType::LumMod, 2000 });
        aComplexColor.addTransformation({ model::TransformationType::LumOff, 8000 });

        OUString aJSON
            = OStringToOUString(model::color::convertToJSON(aComplexColor), RTL_TEXTENCODING_UTF8);

        // When setting the fill color of that shape, with theme metadata & effects:
        uno::Sequence<beans::PropertyValue> aColorArgs = {
            comphelper::makePropertyValue(u"Color.Color"_ustr,
                                          sal_Int32(0xdae3f3)), // 80% light blue
            comphelper::makePropertyValue(u"Color.ComplexColorJSON"_ustr, uno::Any(aJSON)),
        };
        dispatchCommand(mxComponent, u".uno:Color"_ustr, aColorArgs);
    }

    // Then make sure the theme "metadata" is set in the document model:
    pView->EndTextEditCurrentView();
    CPPUNIT_ASSERT(!pView->IsTextEdit());
    uno::Reference<container::XEnumerationAccess> xShapeParaAccess(xShape, uno::UNO_QUERY);
    uno::Reference<container::XEnumerationAccess> xPara(
        xShapeParaAccess->createEnumeration()->nextElement(), uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xPortion(xPara->createEnumeration()->nextElement(),
                                                 uno::UNO_QUERY);
    {
        uno::Reference<util::XComplexColor> xComplexColor;
        CPPUNIT_ASSERT(xPortion->getPropertyValue(u"CharComplexColor"_ustr) >>= xComplexColor);
        CPPUNIT_ASSERT(xComplexColor.is());
        auto aComplexColor = model::color::getFromXComplexColor(xComplexColor);
        CPPUNIT_ASSERT_EQUAL(model::ThemeColorType::Accent1, aComplexColor.getThemeColorType());
        CPPUNIT_ASSERT_EQUAL(model::TransformationType::LumMod,
                             aComplexColor.getTransformations()[0].meType);
        CPPUNIT_ASSERT_EQUAL(sal_Int16(2000), aComplexColor.getTransformations()[0].mnValue);
        CPPUNIT_ASSERT_EQUAL(model::TransformationType::LumOff,
                             aComplexColor.getTransformations()[1].meType);
        CPPUNIT_ASSERT_EQUAL(sal_Int16(8000), aComplexColor.getTransformations()[1].mnValue);
    }
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testFillColorTheme)
{
    // Given an Impress document with a selected shape:
    createSdImpressDoc();
    uno::Reference<drawing::XDrawPagesSupplier> xPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xPage(xPagesSupplier->getDrawPages()->getByIndex(0),
                                             uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xShape(xPage->getByIndex(0), uno::UNO_QUERY);
    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<view::XSelectionSupplier> xController(xModel->getCurrentController(),
                                                         uno::UNO_QUERY);
    xController->select(uno::Any(xShape));

    // Change fill color
    {
        model::ComplexColor aComplexColor;
        aComplexColor.setThemeColor(model::ThemeColorType::Accent1);
        aComplexColor.addTransformation({ model::TransformationType::LumMod, 4000 });
        aComplexColor.addTransformation({ model::TransformationType::LumOff, 6000 });

        OUString aJSON
            = OStringToOUString(model::color::convertToJSON(aComplexColor), RTL_TEXTENCODING_UTF8);

        // When setting the fill color of that shape, with theme metadata & effects:
        uno::Sequence<beans::PropertyValue> aColorArgs = {
            comphelper::makePropertyValue(u"FillColor.Color"_ustr, sal_Int32(0xed7d31)), // orange
            comphelper::makePropertyValue(u"FillColor.ComplexColorJSON"_ustr,
                                          uno::Any(aJSON)), // accent 1
        };
        dispatchCommand(mxComponent, u".uno:FillColor"_ustr, aColorArgs);
    }

    // Then make sure the theme index is not lost when the sidebar sets it:
    {
        uno::Reference<util::XComplexColor> xComplexColor;
        CPPUNIT_ASSERT(xShape->getPropertyValue(u"FillComplexColor"_ustr) >>= xComplexColor);
        CPPUNIT_ASSERT(xComplexColor.is());
        auto aComplexColor = model::color::getFromXComplexColor(xComplexColor);
        CPPUNIT_ASSERT_EQUAL(model::ThemeColorType::Accent1, aComplexColor.getThemeColorType());
        CPPUNIT_ASSERT_EQUAL(model::TransformationType::LumMod,
                             aComplexColor.getTransformations()[0].meType);
        CPPUNIT_ASSERT_EQUAL(sal_Int16(4000), aComplexColor.getTransformations()[0].mnValue);
        CPPUNIT_ASSERT_EQUAL(model::TransformationType::LumOff,
                             aComplexColor.getTransformations()[1].meType);
        CPPUNIT_ASSERT_EQUAL(sal_Int16(6000), aComplexColor.getTransformations()[1].mnValue);
    }
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf163805)
{
    createSdImpressDoc();

    uno::Reference<beans::XPropertySet> xShape(getShapeFromPage(0, 0));

    drawing::FillStyle eFillStyle;
    Color aColor;
    CPPUNIT_ASSERT(xShape->getPropertyValue(u"FillStyle"_ustr) >>= eFillStyle);
    CPPUNIT_ASSERT_EQUAL(int(drawing::FillStyle_NONE), static_cast<int>(eFillStyle));
    CPPUNIT_ASSERT(xShape->getPropertyValue(u"FillColor"_ustr) >>= aColor);
    CPPUNIT_ASSERT_EQUAL(Color(0x729fcf), aColor);

    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<view::XSelectionSupplier> xController(xModel->getCurrentController(),
                                                         uno::UNO_QUERY);
    xController->select(uno::Any(xShape));

    uno::Sequence<beans::PropertyValue> aColorArgs
        = { comphelper::makePropertyValue(u"FillColor.Color"_ustr, sal_Int32(0x800000)) };
    dispatchCommand(mxComponent, u".uno:FillColor"_ustr, aColorArgs);

    CPPUNIT_ASSERT(xShape->getPropertyValue(u"FillStyle"_ustr) >>= eFillStyle);

    // Without the fix in place, this test would have failed with
    // - Expected: 1
    // - Actual  : 0
    CPPUNIT_ASSERT_EQUAL(int(drawing::FillStyle_SOLID), static_cast<int>(eFillStyle));
    CPPUNIT_ASSERT(xShape->getPropertyValue(u"FillColor"_ustr) >>= aColor);
    CPPUNIT_ASSERT_EQUAL(COL_RED, aColor);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testFillColorNoColor)
{
    // Given an empty Impress document:
    createSdImpressDoc();
    auto pImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    sd::ViewShell* pViewShell = pImpressDocument->GetDocShell()->GetViewShell();
    SfxDispatcher* pDispatcher = pViewShell->GetViewFrame()->GetDispatcher();

    // When dispatching a fill color that only has a fill style (no color), then make sure we don't
    // crash:
    XFillStyleItem aXFillStyleItem(drawing::FillStyle_NONE);
    pDispatcher->ExecuteList(SID_ATTR_FILL_COLOR, SfxCallMode::RECORD, { &aXFillStyleItem });
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf153161)
{
    createSdImpressDoc("odp/tdf153161_FlushToSave.odp");

    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage(xDrawPagesSupplier->getDrawPages()->getByIndex(0),
                                                 uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xShape(xDrawPage->getByIndex(1), uno::UNO_QUERY);
    OUString sOldText(xShape->getString());
    CPPUNIT_ASSERT(sOldText.startsWith(u"在没有版本控制系统的时期"));

    // Type something, getting into text editing mode (appending) automatically
    insertStringToObject(1, u"Foo Bar", /*bUseEscape*/ false);

    saveAndReload(u"impress8"_ustr);

    xDrawPagesSupplier.set(mxComponent, uno::UNO_QUERY);
    xDrawPage.set(xDrawPagesSupplier->getDrawPages()->getByIndex(0), uno::UNO_QUERY);
    xShape.set(xDrawPage->getByIndex(1), uno::UNO_QUERY);
    OUString sExpectedText = sOldText + "Foo Bar";
    CPPUNIT_ASSERT_EQUAL(sExpectedText, xShape->getString());
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf148810)
{
    createSdImpressDoc("pptx/tdf148810_PARA_OUTLLEVEL.pptx");

    // type something to get into text editing mode (instead of shape selection).
    insertStringToObject(1, u"x", /*bUseEscape*/ false);

    auto pXImpressDocument = dynamic_cast<SdXImpressDocument*>(mxComponent.get());
    typeKey(pXImpressDocument, KEY_HOME);

    typeKey(pXImpressDocument, KEY_BACKSPACE);

    uno::Reference<beans::XPropertySet> xShape(getShapeFromPage(1, 0));
    uno::Reference<text::XTextRange> xParagraph(getParagraphFromShape(0, xShape));
    uno::Reference<beans::XPropertySet> xPropSet(xParagraph, uno::UNO_QUERY_THROW);
    sal_Int16 nNumberingLevel = -1;
    xPropSet->getPropertyValue(u"NumberingLevel"_ustr) >>= nNumberingLevel;
    CPPUNIT_ASSERT_EQUAL(sal_Int16(0), nNumberingLevel);

    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    nNumberingLevel = -1;
    xPropSet->getPropertyValue(u"NumberingLevel"_ustr) >>= nNumberingLevel;

    // Without the fix in place, this test would have failed with
    // - Expected: 0
    // - Actual  : -1
    CPPUNIT_ASSERT_EQUAL(sal_Int16(0), nNumberingLevel);
}

CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testTdf127696)
{
    createSdImpressDoc();

    dispatchCommand(mxComponent, u".uno:InsertPage"_ustr, {});

    insertStringToObject(0, u"Test", /*bUseEscape*/ false);
    dispatchCommand(mxComponent, u".uno:SelectAll"_ustr, {});
    dispatchCommand(mxComponent, u".uno:OutlineFont"_ustr, {});

    // Save it as PPTX and load it again.
    saveAndReload(u"Impress Office Open XML"_ustr);

    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage(xDrawPagesSupplier->getDrawPages()->getByIndex(1),
                                                 uno::UNO_QUERY);

    uno::Reference<beans::XPropertySet> xShape(xDrawPage->getByIndex(0), uno::UNO_QUERY);

    uno::Reference<text::XText> xText
        = uno::Reference<text::XTextRange>(xShape, uno::UNO_QUERY_THROW)->getText();
    CPPUNIT_ASSERT_MESSAGE("Not a text shape", xText.is());

    uno::Reference<container::XEnumerationAccess> paraEnumAccess(xText, uno::UNO_QUERY);
    uno::Reference<container::XEnumeration> paraEnum(paraEnumAccess->createEnumeration());

    uno::Reference<text::XTextRange> xParagraph(paraEnum->nextElement(), uno::UNO_QUERY_THROW);

    uno::Reference<container::XEnumerationAccess> runEnumAccess(xParagraph, uno::UNO_QUERY);
    uno::Reference<container::XEnumeration> runEnum = runEnumAccess->createEnumeration();

    uno::Reference<text::XTextRange> xRun(runEnum->nextElement(), uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xPropSet(xRun, uno::UNO_QUERY_THROW);

    bool bContoured = false;
    xPropSet->getPropertyValue(u"CharContoured"_ustr) >>= bContoured;
    CPPUNIT_ASSERT(bContoured);
}

// TODO - Readd when we implement style level fill color and set that for default style
/*CPPUNIT_TEST_FIXTURE(SdUiImpressTest, testThemeShapeInsert)
{
    // Given a document with a theme, accent1 color is set to 0x000004:
    createSdImpressDoc();
    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage(xDrawPagesSupplier->getDrawPages()->getByIndex(0),
                                                 uno::UNO_QUERY);
    uno::Reference<drawing::XMasterPageTarget> xMasterPageTarget(xDrawPage, uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xMasterPage(xMasterPageTarget->getMasterPage(),
                                                    uno::UNO_QUERY);

    auto pTheme = std::make_shared<model::Theme>("mytheme");
    auto pColorSet = std::make_shared<model::ColorSet>("mycolorscheme");
    pColorSet->add(model::ThemeColorType::Dark1, 0x0);
    pColorSet->add(model::ThemeColorType::Light1, 0x1);
    pColorSet->add(model::ThemeColorType::Dark2, 0x2);
    pColorSet->add(model::ThemeColorType::Light2, 0x3);
    pColorSet->add(model::ThemeColorType::Accent1, 0x4);
    pColorSet->add(model::ThemeColorType::Accent2, 0x5);
    pColorSet->add(model::ThemeColorType::Accent3, 0x6);
    pColorSet->add(model::ThemeColorType::Accent4, 0x7);
    pColorSet->add(model::ThemeColorType::Accent5, 0x8);
    pColorSet->add(model::ThemeColorType::Accent6, 0x9);
    pColorSet->add(model::ThemeColorType::Hyperlink, 0xa);
    pColorSet->add(model::ThemeColorType::FollowedHyperlink, 0xb);
    pTheme->setColorSet(pColorSet);

    xMasterPage->setPropertyValue("Theme", uno::Any(model::theme::createXTheme(pTheme)));

    // When inserting a shape:
    uno::Sequence<beans::PropertyValue> aArgs = {
        comphelper::makePropertyValue("CreateDirectly", true),
    };
    dispatchCommand(mxComponent, ".uno:BasicShapes.round-rectangle", aArgs);

    // Then make sure the that fill color of the last shape is the accent1 color:
    sal_Int32 nShapeIndex = xDrawPage->getCount() - 1;
    uno::Reference<beans::XPropertySet> xShape(xDrawPage->getByIndex(nShapeIndex), uno::UNO_QUERY);
    sal_Int32 nFillColor{};
    xShape->getPropertyValue("FillColor") >>= nFillColor;
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 0 / 0x000004 (~black)
    // - Actual  : 7512015 / 0x729fcf (~blue)
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0x4), nFillColor);
}*/

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
