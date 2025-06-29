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

#include <accessibility/vclxaccessiblebutton.hxx>
#include <svdata.hxx>
#include <strings.hrc>

#include <comphelper/accessiblecontexthelper.hxx>
#include <comphelper/accessiblekeybindinghelper.hxx>
#include <com/sun/star/awt/KeyModifier.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>

#include <vcl/accessibility/strings.hxx>
#include <vcl/toolkit/button.hxx>
#include <vcl/event.hxx>
#include <vcl/vclevent.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::accessibility;
using namespace ::comphelper;


// VCLXAccessibleButton


void VCLXAccessibleButton::ProcessWindowEvent( const VclWindowEvent& rVclWindowEvent )
{
    switch ( rVclWindowEvent.GetId() )
    {
        case VclEventId::PushbuttonToggle:
        {
            Any aOldValue;
            Any aNewValue;

            VclPtr< PushButton > pButton = GetAs< PushButton >();
            if ( pButton && pButton->GetState() == TRISTATE_TRUE )
                aNewValue <<= AccessibleStateType::CHECKED;
            else
                aOldValue <<= AccessibleStateType::CHECKED;

            NotifyAccessibleEvent( AccessibleEventId::STATE_CHANGED, aOldValue, aNewValue );
        }
        break;
        default:
            VCLXAccessibleTextComponent::ProcessWindowEvent( rVclWindowEvent );
   }
}


void VCLXAccessibleButton::FillAccessibleStateSet( sal_Int64& rStateSet )
{
    VCLXAccessibleTextComponent::FillAccessibleStateSet( rStateSet );

    VclPtr< PushButton > pButton = GetAs< PushButton >();
    if ( !pButton )
        return;

    rStateSet |= AccessibleStateType::FOCUSABLE;

    if (pButton->isToggleButton())
        rStateSet |= AccessibleStateType::CHECKABLE;

    if ( pButton->GetState() == TRISTATE_TRUE )
        rStateSet |= AccessibleStateType::CHECKED;

    if ( pButton->IsPressed() )
        rStateSet |= AccessibleStateType::PRESSED;

    // IA2 CWS: if the button has a popup menu, it should has the state EXPANDABLE
    if( pButton->GetType() == WindowType::MENUBUTTON )
    {
        rStateSet |= AccessibleStateType::EXPANDABLE;
    }
    if( pButton->GetStyle() & WB_DEFBUTTON )
    {
        rStateSet |= AccessibleStateType::DEFAULT;
    }
}


// XServiceInfo


OUString VCLXAccessibleButton::getImplementationName()
{
    return u"com.sun.star.comp.toolkit.AccessibleButton"_ustr;
}


Sequence< OUString > VCLXAccessibleButton::getSupportedServiceNames()
{
    return { u"com.sun.star.awt.AccessibleButton"_ustr };
}

// XAccessibleContext


OUString VCLXAccessibleButton::getAccessibleName(  )
{
    OUString aName( VCLXAccessibleTextComponent::getAccessibleName() );
    sal_Int32 nLength = aName.getLength();

    if ( nLength >= 3 && aName.match( "...", nLength - 3 ) )
    {
        if ( nLength == 3 )
        {
            // it's a browse button
            aName = VclResId( RID_STR_ACC_NAME_BROWSEBUTTON );
        }
        else
        {
            // remove the three trailing dots
            aName = aName.copy( 0, nLength - 3 );
        }
    }
    else if ( nLength >= 3 && aName.match( "<< ", 0 ) )
    {
        // remove the leading symbols
        aName = aName.copy( 3, nLength - 3 );
    }
    else if ( nLength >= 3 && aName.match( " >>", nLength - 3 ) )
    {
        // remove the trailing symbols
        aName = aName.copy( 0, nLength - 3 );
    }

    return aName;
}


// XAccessibleAction


sal_Int32 VCLXAccessibleButton::getAccessibleActionCount( )
{
    OExternalLockGuard aGuard( this );

    return 1;
}


sal_Bool VCLXAccessibleButton::doAccessibleAction ( sal_Int32 nIndex )
{
    OExternalLockGuard aGuard( this );

    if ( nIndex != 0 )
        throw IndexOutOfBoundsException();

    VclPtr< PushButton > pButton = GetAs< PushButton >();
    if ( pButton )
    {
        if (pButton->isToggleButton())
        {
            // PushButton::Click doesn't toggle when it's a toggle button
            pButton->Check(!pButton->IsChecked());
            pButton->Toggle();
        }
        else
        {
            pButton->Click();
        }
    }

    return true;
}


OUString VCLXAccessibleButton::getAccessibleActionDescription ( sal_Int32 nIndex )
{
    OExternalLockGuard aGuard( this );

    if ( nIndex != 0 )
        throw IndexOutOfBoundsException();

    return RID_STR_ACC_ACTION_CLICK;
}


Reference< XAccessibleKeyBinding > VCLXAccessibleButton::getAccessibleActionKeyBinding( sal_Int32 nIndex )
{
    OExternalLockGuard aGuard( this );

    if ( nIndex != 0 )
        throw IndexOutOfBoundsException();

    rtl::Reference<OAccessibleKeyBindingHelper> pKeyBindingHelper = new OAccessibleKeyBindingHelper();

    VclPtr<vcl::Window> pWindow = GetWindow();
    if ( pWindow )
    {
        KeyEvent aKeyEvent = pWindow->GetActivationKey();
        vcl::KeyCode aKeyCode = aKeyEvent.GetKeyCode();
        if ( aKeyCode.GetCode() != 0 )
        {
            awt::KeyStroke aKeyStroke;
            aKeyStroke.Modifiers = 0;
            if ( aKeyCode.IsShift() )
                aKeyStroke.Modifiers |= awt::KeyModifier::SHIFT;
            if ( aKeyCode.IsMod1() )
                aKeyStroke.Modifiers |= awt::KeyModifier::MOD1;
            if ( aKeyCode.IsMod2() )
                aKeyStroke.Modifiers |= awt::KeyModifier::MOD2;
            if ( aKeyCode.IsMod3() )
                aKeyStroke.Modifiers |= awt::KeyModifier::MOD3;
            aKeyStroke.KeyCode = aKeyCode.GetCode();
            aKeyStroke.KeyChar = aKeyEvent.GetCharCode();
            aKeyStroke.KeyFunc = static_cast< sal_Int16 >( aKeyCode.GetFunction() );
            pKeyBindingHelper->AddKeyBinding( aKeyStroke );
        }
    }

    return pKeyBindingHelper;
}


// XAccessibleValue


Any VCLXAccessibleButton::getCurrentValue(  )
{
    OExternalLockGuard aGuard( this );

    Any aValue;

    VclPtr< PushButton > pButton = GetAs< PushButton >();
    if ( pButton )
        aValue <<= static_cast<sal_Int32>(pButton->IsPressed());

    return aValue;
}


sal_Bool VCLXAccessibleButton::setCurrentValue( const Any& aNumber )
{
    OExternalLockGuard aGuard( this );

    bool bReturn = false;

    VclPtr< PushButton > pButton = GetAs< PushButton >();
    if ( pButton )
    {
        sal_Int32 nValue = 0;
        OSL_VERIFY( aNumber >>= nValue );

        if ( nValue < 0 )
            nValue = 0;
        else if ( nValue > 1 )
            nValue = 1;

        pButton->SetPressed( nValue == 1 );
        bReturn = true;
    }

    return bReturn;
}


Any VCLXAccessibleButton::getMaximumValue(  )
{
    OExternalLockGuard aGuard( this );

    Any aValue;
    aValue <<= sal_Int32(1);

    return aValue;
}


Any VCLXAccessibleButton::getMinimumValue(  )
{
    OExternalLockGuard aGuard( this );

    Any aValue;
    aValue <<= sal_Int32(0);

    return aValue;
}

Any VCLXAccessibleButton::getMinimumIncrement(  )
{
    OExternalLockGuard aGuard( this );

    Any aValue;
    aValue <<= sal_Int32(1);

    return aValue;
}


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
