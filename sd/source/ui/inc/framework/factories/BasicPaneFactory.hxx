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

#pragma once

#include <com/sun/star/drawing/framework/XResourceFactory.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <framework/ConfigurationChangeListener.hxx>
#include <comphelper/compbase.hxx>
#include <unotools/weakref.hxx>
#include <rtl/ref.hxx>

#include <memory>

namespace com::sun::star::uno { class XComponentContext; }

namespace sd {
class DrawController;
class ViewShellBase;
}

namespace sd::framework {
class ConfigurationController;

typedef cppu::ImplInheritanceHelper <
    sd::framework::ConfigurationChangeListener,
    css::drawing::framework::XResourceFactory
    > BasicPaneFactoryInterfaceBase;

/** This factory provides the frequently used standard panes
        private:resource/pane/CenterPane
        private:resource/pane/FullScreenPane
        private:resource/pane/LeftImpressPane
        private:resource/pane/BottomImpressPane
        private:resource/pane/LeftDrawPane
    There are two left panes because this is (seems to be) the only way to
    show different titles for the left pane in Draw and Impress.
*/
class BasicPaneFactory final
    : public BasicPaneFactoryInterfaceBase
{
public:
    explicit BasicPaneFactory(
        const rtl::Reference<::sd::DrawController>& rxController);
    virtual ~BasicPaneFactory() override;

    virtual void disposing(std::unique_lock<std::mutex>&) override;

    // XResourceFactory

    virtual css::uno::Reference<css::drawing::framework::XResource>
        SAL_CALL createResource (
            const css::uno::Reference<css::drawing::framework::XResourceId>& rxPaneId) override;

    virtual void SAL_CALL
        releaseResource (
            const css::uno::Reference<css::drawing::framework::XResource>& rxPane) override;

    // ConfigurationChangeListener

    virtual void notifyConfigurationChange (
        const sd::framework::ConfigurationChangeEvent& rEvent) override;

    // lang::XEventListener

    virtual void SAL_CALL disposing (
        const css::lang::EventObject& rEventObject) override;

private:
    unotools::WeakReference<sd::framework::ConfigurationController>
        mxConfigurationControllerWeak;
    ViewShellBase* mpViewShellBase;
    class PaneDescriptor;
    using PaneContainer = std::vector<PaneDescriptor>;

    PaneContainer maPaneContainer;

    /** Create a new instance of FrameWindowPane.
        @param rPaneId
            There is only one frame window so this id is just checked to
            have the correct value.
    */
    css::uno::Reference<css::drawing::framework::XResource>
        CreateFrameWindowPane (
            const css::uno::Reference<css::drawing::framework::XResourceId>& rxPaneId);

    /** Create a new pane that represents the center pane in full screen
        mode.
    */
    css::uno::Reference<css::drawing::framework::XResource>
        CreateFullScreenPane (
            const css::uno::Reference<css::drawing::framework::XResourceId>& rxPaneId);

    /** Create a new instance of ChildWindowPane.
        @param rPaneId
            The ResourceURL member defines which side pane to create.
    */
    css::uno::Reference<css::drawing::framework::XResource>
        CreateChildWindowPane (
            const css::uno::Reference<
                css::drawing::framework::XResourceId>& rxPaneId,
            const PaneDescriptor& rDescriptor);

    /// @throws css::lang::DisposedException
    void ThrowIfDisposed() const;
};

} // end of namespace sd::framework

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
