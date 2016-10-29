/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "NodeViewerContext.h"

#include <QMouseEvent>
#include <QStyle>
#include <QToolBar>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QFrame>
#include <QUndoStack>
#include <QUndoCommand>
#include <ofxKeySyms.h>

#include "Engine/EffectInstance.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"
#include "Engine/Plugin.h"
#include "Engine/ViewerNode.h"

#include "Gui/ActionShortcuts.h"
#include "Gui/ColoredFrame.h"
#include "Gui/ClickableLabel.h"
#include "Gui/Gui.h"
#include "Gui/GuiApplicationManager.h"
#include "Gui/GuiDefines.h"
#include "Gui/GuiMacros.h"
#include "Gui/KnobGui.h"
#include "Gui/NodeGui.h"
#include "Gui/NodeSettingsPanel.h"
#include "Gui/QtEnumConvert.h"
#include "Gui/ViewerGL.h"
#include "Gui/ViewerTab.h"
#include "Gui/ViewerToolButton.h"

NATRON_NAMESPACE_ENTER;

struct NodeViewerContextPrivate
{
    Q_DECLARE_TR_FUNCTIONS(NodeViewerContext)

public:
    NodeViewerContext* publicInterface; // can not be a smart ptr
    NodeGuiWPtr node;
    ViewerGL* viewer;
    ViewerTab* viewerTab;
    std::map<KnobIWPtr, KnobGuiPtr> knobsMapping;
    QString currentRole, currentTool;
    QToolBar* toolbar;
    std::map<QString, ViewerToolButton*> toolButtons;
    QWidget* mainContainer;
    QHBoxLayout* mainContainerLayout;
    Label* nodeLabel;
    QWidget* widgetsContainer;
    QVBoxLayout* widgetsContainerLayout;

    // This is specific to the viewer node only
    QWidget* playerContainer;
    QHBoxLayout* playerLayout;

    NodeViewerContextPrivate(NodeViewerContext* pi,
                             const NodeGuiPtr& node,
                             ViewerTab* viewer)
        : publicInterface(pi)
        , node(node)
        , viewer( viewer->getViewer() )
        , viewerTab(viewer)
        , knobsMapping()
        , currentRole()
        , currentTool()
        , toolbar(0)
        , toolButtons()
        , mainContainer(0)
        , mainContainerLayout(0)
        , nodeLabel(0)
        , widgetsContainer(0)
        , widgetsContainerLayout(0)
        , playerContainer(0)
        , playerLayout(0)
    {
    }

    void createKnobInternal(const KnobIPtr& knob,
                            QWidget*& lastRowContainer,
                            QHBoxLayout*& lastRowLayout,
                            KnobsVec& knobsOnSameLine);

    void createKnobs(const KnobsVec& knobsUi);

    NodeGuiPtr getNode() const
    {
        return node.lock();
    }

    /**
     * @brief Add a new tool to the toolbutton corresponding to the given roleID.
     * If a ViewerToolButton for this roleID does not exist yet, it is created and the
     * given roleShortcutID is associated to it.
     * The shortcut for this button will be added to the Shorcut Editor with by default
     * the given modifiers and symbols (as defined in <ofxKeySyms.h>).
     * The tool will have the given label, and when hovering the button with the mouse, the user
     * will receive the hintToolTip help.
     * Optionnally, a path to an icon can be specified for this button.
     **/
    QAction* addToolBarTool(const std::string& toolID,
                            const std::string& roleID,
                            const std::string& roleShortcutID,
                            const std::string& label,
                            const std::string& hintToolTip,
                            const std::string& iconPath,
                            ViewerToolButton** toolButton);

    void onToolActionTriggeredInternal(QAction* action, bool notifyNode);

    void toggleToolsSelection(ViewerToolButton* selected)
    {
        for (std::map<QString, ViewerToolButton*>::iterator it = toolButtons.begin(); it != toolButtons.end(); ++it) {
            if (it->second == selected) {
                it->second->setIsSelected(true);
            } else {
                it->second->setIsSelected(false);
                if ( it->second->isDown() ) {
                    it->second->setDown(false);
                }
            }
        }
    }
};

NodeViewerContext::NodeViewerContext(const NodeGuiPtr& node,
                                     ViewerTab* viewer)
    : QObject()
    , KnobGuiContainerI()
    , _imp( new NodeViewerContextPrivate(this, node, viewer) )
{
}

NodeViewerContext::~NodeViewerContext()
{
}

void
NodeViewerContext::createGui()
{
    QObject::connect( _imp->viewer, SIGNAL(selectionRectangleChanged(bool)), this, SLOT(updateSelectionFromViewerSelectionRectangle(bool)), Qt::UniqueConnection );
    QObject::connect( _imp->viewer, SIGNAL(selectionCleared()), this, SLOT(onViewerSelectionCleared()), Qt::UniqueConnection );
    NodeGuiPtr node = _imp->getNode();
    QObject::connect( node.get(), SIGNAL(settingsPanelClosed(bool)), this, SLOT(onNodeSettingsPanelClosed(bool)), Qt::UniqueConnection );
    KnobsVec knobsOrdered = node->getNode()->getEffectInstance()->getViewerUIKnobs();

    ViewerNodePtr isViewerNode = node->getNode()->isEffectViewerNode();

    if ( !knobsOrdered.empty() ) {
        if (!isViewerNode) {
            _imp->mainContainer = new ColoredFrame(_imp->viewer);
        } else {
             _imp->mainContainer = new QWidget(_imp->viewer);
        }
        _imp->mainContainerLayout = new QHBoxLayout(_imp->mainContainer);
        _imp->mainContainerLayout->setContentsMargins(0, 0, 0, 0);
        _imp->mainContainerLayout->setSpacing(0);
        if (!isViewerNode) {
            _imp->nodeLabel = new Label(QString::fromUtf8( node->getNode()->getLabel().c_str() ), _imp->mainContainer);
            QObject::connect( node->getNode().get(), SIGNAL(labelChanged(QString)), _imp->nodeLabel, SLOT(setText(QString)) );
        }
        _imp->widgetsContainer = new QWidget(_imp->mainContainer);
        _imp->widgetsContainerLayout = new QVBoxLayout(_imp->widgetsContainer);
        _imp->widgetsContainerLayout->setContentsMargins(0, 0, 0, 0);
        _imp->widgetsContainerLayout->setSpacing(0);
        _imp->mainContainerLayout->addWidget(_imp->widgetsContainer);
        if (_imp->nodeLabel) {
            _imp->mainContainerLayout->addWidget(_imp->nodeLabel);
        }
        onNodeColorChanged( node->getCurrentColor() );
        QObject::connect( node.get(), SIGNAL(colorChanged(QColor)), this, SLOT(onNodeColorChanged(QColor)) );
        setContainerWidget(_imp->mainContainer);
        _imp->createKnobs(knobsOrdered);
    }

    const KnobsVec& allKnobs = node->getNode()->getKnobs();
    KnobPagePtr toolbarPage;
    for (KnobsVec::const_iterator it = allKnobs.begin(); it != allKnobs.end(); ++it) {
        KnobPagePtr isPage = toKnobPage(*it);
        if ( isPage && isPage->getIsToolBar() ) {
            toolbarPage = isPage;
            break;
        }
    }
    if (toolbarPage) {
        KnobsVec pageChildren = toolbarPage->getChildren();
        if ( !pageChildren.empty() ) {
            _imp->toolbar = new QToolBar(_imp->viewer);
            _imp->toolbar->setOrientation(Qt::Vertical);

            for (std::size_t i = 0; i < pageChildren.size(); ++i) {
                KnobGroupPtr isGroup = toKnobGroup( pageChildren[i] );
                if (isGroup) {
                    QObject::connect( isGroup->getSignalSlotHandler().get(), SIGNAL(valueChanged(ViewSpec,int,int)), this, SLOT(onToolGroupValueChanged(ViewSpec,int,int)) );
                    KnobsVec toolButtonChildren = isGroup->getChildren();
                    ViewerToolButton* createdToolButton = 0;
                    QString currentActionForGroup;
                    for (std::size_t j = 0; j < toolButtonChildren.size(); ++j) {
                        KnobButtonPtr isButton = toKnobButton( toolButtonChildren[j] );
                        if (isButton) {
                            QObject::connect( isButton->getSignalSlotHandler().get(), SIGNAL(valueChanged(ViewSpec,int,int)), this, SLOT(onToolActionValueChanged(ViewSpec,int,int)) );
                            const std::string& roleShortcutID = isGroup->getName();
                            QAction* act = _imp->addToolBarTool(isButton->getName(), isGroup->getName(), roleShortcutID, isButton->getLabel(), isButton->getHintToolTip(), isButton->getIconLabel(), &createdToolButton);
                            if ( act && createdToolButton && isButton->getValue() ) {
                                createdToolButton->setDefaultAction(act);
                                currentActionForGroup = QString::fromUtf8( isButton->getName().c_str() );
                            }
                        }
                    }
                    if ( isGroup->getValue() ) {
                        _imp->currentTool = currentActionForGroup;
                        _imp->currentRole = QString::fromUtf8( isGroup->getName().c_str() );
                        if (createdToolButton) {
                            createdToolButton->setDown(true);
                            createdToolButton->setIsSelected(true);
                        }
                    }
                }
            }
        }
    }
} // NodeViewerContext::createGui

static void
addSpacer(QBoxLayout* layout)
{
    layout->addSpacing( TO_DPIX(5) );
    QFrame* line = new QFrame( layout->parentWidget() );
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Raised);
    QPalette palette;
    palette.setColor(QPalette::Foreground, Qt::black);
    line->setPalette(palette);
    layout->addWidget(line);
    layout->addSpacing( TO_DPIX(5) );
}

void
NodeViewerContext::onNodeColorChanged(const QColor& color)
{
    QString labelStyle = QString::fromUtf8("Label { color: rgb(%1, %2, %3); }").arg( color.red() ).arg( color.green() ).arg( color.blue() );
    if (_imp->nodeLabel) {
        _imp->nodeLabel->setStyleSheet(labelStyle);
    }
    ColoredFrame* w = dynamic_cast<ColoredFrame*>(_imp->mainContainer);
    if (w) {
        w->setFrameColor(color);
    }
}

void
NodeViewerContext::onNodeSettingsPanelClosed(bool closed)
{
    if (!_imp->viewerTab) {
        return;
    }
    NodeGuiPtr node = _imp->node.lock();
    if (closed) {
        _imp->viewerTab->removeNodeViewerInterface(node, false /*permanantly*/, true /*setAnother*/);
    } else {
        // Set the viewer interface for this plug-in to be the one of this node
        _imp->viewerTab->setPluginViewerInterface(node);
    }
}

int
NodeViewerContext::getItemsSpacingOnSameLine() const
{
    return 0;
}

void
NodeViewerContextPrivate::createKnobInternal(const KnobIPtr& knob,
                                             QWidget*& lastRowContainer,
                                             QHBoxLayout*& lastRowLayout,
                                             KnobsVec& knobsOnSameLine)
{
    KnobGuiPtr ret( appPTR->createGuiForKnob(knob, publicInterface) );
    if (!ret) {
        assert(false);
        return;
    }
    ret->initialize();

    knobsMapping.insert( std::make_pair(knob, ret) );

    ViewerContextLayoutTypeEnum layoutType = knob->getInViewerContextLayoutType();

    KnobClickableLabel* label = 0;
    std::string inViewerLabel = knob->getInViewerContextLabel();
    std::string inViewerLabelIcon = knob->getInViewerContextIconFilePath(false);
    QWidget* labelContainer = 0;
    if ( (!inViewerLabel.empty() || !inViewerLabelIcon.empty()) && ret->shouldCreateLabel() ) {
        labelContainer = new QWidget(widgetsContainer);
        QHBoxLayout* labelLayout = new QHBoxLayout(labelContainer);
        labelLayout->setContentsMargins(0, 0, TO_DPIX(3), 0 );
        labelLayout->setSpacing( TO_DPIY(2) );

        label = new KnobClickableLabel(QString(), ret, labelContainer);
        KnobGuiContainerHelper::setLabelFromTextAndIcon(label, QString::fromUtf8(inViewerLabel.c_str()) + QString::fromUtf8(":"), QString::fromUtf8(inViewerLabelIcon.c_str()), ret->isLabelBold());
        QObject::connect( label, SIGNAL(clicked(bool)), ret.get(), SIGNAL(labelClicked(bool)) );

        labelLayout->addWidget(label);
    }
    if (layoutType == eViewerContextLayoutTypeStretchBefore) {
        lastRowLayout->addStretch();
    }
    ret->createGUI(lastRowContainer, labelContainer, label, 0 /*warningIndicator*/, lastRowLayout, layoutType == eViewerContextLayoutTypeAddNewLine, 0, knobsOnSameLine);

    if (layoutType == eViewerContextLayoutTypeAddNewLine) {
        knobsOnSameLine.clear();
        lastRowLayout->addStretch();
        lastRowContainer = new QWidget(widgetsContainer);
        lastRowLayout = new QHBoxLayout(lastRowContainer);
        lastRowLayout->setContentsMargins(TO_DPIX(3), TO_DPIY(2), 0, 0);
        lastRowLayout->setSpacing(0);
        widgetsContainerLayout->addWidget(lastRowContainer);
    } else {
        knobsOnSameLine.push_back(knob);
        if ( layoutType == eViewerContextLayoutTypeSeparator ) {
            addSpacer(lastRowLayout);
        } else if ( layoutType == eViewerContextLayoutTypeStretchAfter ) {
            lastRowLayout->addStretch();
        } else if ( layoutType == eViewerContextLayoutTypeSpacing ) {
            int spacing = knob->getInViewerContextItemSpacing();
            lastRowLayout->addSpacing( TO_DPIX(spacing) );
        }

    } // makeNewLine


    ret->setEnabledSlot();
    ret->setSecret();


}

void
NodeViewerContextPrivate::createKnobs(const KnobsVec& knobsOrdered)
{
    NodeGuiPtr thisNode = getNode();

    assert( !knobsOrdered.empty() );


    knobsMapping.clear();

    {
        QWidget* lastRowContainer = new QWidget(widgetsContainer);
        QHBoxLayout* lastRowLayout = new QHBoxLayout(lastRowContainer);
        lastRowLayout->setContentsMargins(TO_DPIX(3), TO_DPIY(2), 0, 0);
        lastRowLayout->setSpacing(0);
        widgetsContainerLayout->addWidget(lastRowContainer);

        KnobsVec knobsOnSameLine;
        for (KnobsVec::const_iterator it = knobsOrdered.begin(); it != knobsOrdered.end(); ++it) {
            createKnobInternal(*it, lastRowContainer, lastRowLayout,  knobsOnSameLine);
        }

    }

    ViewerNodePtr isViewer = thisNode->getNode()->isEffectViewerNode();
    if (isViewer) {
        KnobIPtr playerKnob = thisNode->getNode()->getKnobByName(kViewerNodeParamPlayerToolBarPage);
        KnobPagePtr playerPage = toKnobPage(playerKnob);
        assert(playerPage);

        KnobsVec playerKnobs = playerPage->getChildren();
        assert(!playerKnobs.empty());
        playerContainer =  new QWidget(viewerTab);
        playerLayout =  new QHBoxLayout(playerContainer);
        playerLayout->setContentsMargins(0, 0, 0, 0);
        playerLayout->setSpacing(0);
        KnobsVec knobsOnSameLine;
        for (KnobsVec::const_iterator it = playerKnobs.begin(); it != playerKnobs.end(); ++it) {
            createKnobInternal(*it, playerContainer, playerLayout, knobsOnSameLine);
        }

    }

} // NodeViewerContextPrivate::createKnobs

QAction*
NodeViewerContextPrivate::addToolBarTool(const std::string& toolID,
                                         const std::string& roleID,
                                         const std::string& roleShortcutID,
                                         const std::string& label,
                                         const std::string& hintToolTip,
                                         const std::string& iconPath,
                                         ViewerToolButton** createdToolButton)
{
    QString qRoleId = QString::fromUtf8( roleID.c_str() );
    std::map<QString, ViewerToolButton*>::iterator foundToolButton = toolButtons.find(qRoleId);
    ViewerToolButton* toolButton = 0;

    if ( foundToolButton != toolButtons.end() ) {
        toolButton = foundToolButton->second;
    } else {
        toolButton = new ViewerToolButton(toolbar);
        toolbar->addWidget(toolButton);
        toolButtons.insert( std::make_pair(qRoleId, toolButton) );
        QSize rotoToolSize( TO_DPIX(NATRON_LARGE_BUTTON_SIZE), TO_DPIY(NATRON_LARGE_BUTTON_SIZE) );
        toolButton->setFixedSize(rotoToolSize);
        toolButton->setIconSize(rotoToolSize);
        toolButton->setPopupMode(QToolButton::InstantPopup);
        QObject::connect( toolButton, SIGNAL(triggered(QAction*)), publicInterface, SLOT(onToolActionTriggered(QAction*)) );
    }

    *createdToolButton = toolButton;


    QString shortcutGroup = QString::fromUtf8(getNode()->getNode()->getOriginalPlugin()->getPluginShortcutGroup().c_str());
    QIcon icon;
    if ( !iconPath.empty() ) {
        QString iconPathStr = QString::fromUtf8( iconPath.c_str() );
        if ( QFile::exists(iconPathStr) ) {
            QPixmap pix;
            pix.load(iconPathStr);
            if ( !pix.isNull() ) {
                icon.addPixmap(pix);
            }
        }
    }

    //QString labelTouse = icon.isNull() ? QString::fromUtf8(label.c_str()) : QString();
    QAction* action = new QAction(icon, QString::fromUtf8( label.c_str() ), toolButton);
    QStringList data;
    data.push_back(qRoleId);
    data.push_back( QString::fromUtf8( toolID.c_str() ) );
    action->setData(data);
    QString toolTip;
    if ( !hintToolTip.empty() ) {
        toolTip.append( QString::fromUtf8("<p>") );
        toolTip += QString::fromUtf8( hintToolTip.c_str() );
        toolTip.append( QString::fromUtf8("</p>") );
        if ( !roleShortcutID.empty() ) {
            std::list<QKeySequence> keybinds = getKeybind( shortcutGroup, QString::fromUtf8( toolID.c_str() ) );
            if (keybinds.size() >= 1) {
                toolTip += QString::fromUtf8("<p><b>");
                toolTip += tr("Keyboard shortcut: %1").arg( keybinds.front().toString(QKeySequence::NativeText) );
                toolTip += QString::fromUtf8("</b></p>");
            }
        }
        action->setToolTip(toolTip);
    }
    QObject::connect( action, SIGNAL(triggered()), publicInterface, SLOT(onToolActionTriggered()) );
    toolButton->addAction(action);

    return action;
} // NodeViewerContextPrivate::addToolBarTool

QWidget*
NodeViewerContext::getPlayerToolbar() const
{
    return _imp->playerContainer;
}

QToolBar*
NodeViewerContext::getToolBar() const
{
    return _imp->toolbar;
}

const QString&
NodeViewerContext::getCurrentRole() const
{
    return _imp->currentRole;
}

const QString&
NodeViewerContext::getCurrentTool() const
{
    return _imp->currentTool;
}

Gui*
NodeViewerContext::getGui() const
{
    return _imp->viewerTab ? _imp->viewerTab->getGui() : 0;
}

const QUndoCommand*
NodeViewerContext::getLastUndoCommand() const
{
    NodeSettingsPanel* panel = _imp->getNode()->getSettingPanel();

    if (panel) {
        return panel->getLastUndoCommand();
    }

    return 0;
}

void
NodeViewerContext::pushUndoCommand(QUndoCommand* cmd)
{
    NodeSettingsPanel* panel = _imp->getNode()->getSettingPanel();
    if (!panel) {
        _imp->getNode()->ensurePanelCreated();
        panel = _imp->getNode()->getSettingPanel();
        if (panel) {
            panel->setClosed(true);
        }
    }
    if (panel) {
        panel->pushUndoCommand(cmd);
    }
}

KnobGuiPtr
NodeViewerContext::getKnobGui(const KnobIPtr& knob) const
{
    std::map<KnobIWPtr, KnobGuiPtr>::const_iterator found =  _imp->knobsMapping.find(knob);

    if ( found == _imp->knobsMapping.end() ) {
        return KnobGuiPtr();
    }

    return found->second;
}

void
NodeViewerContext::onToolButtonShortcutPressed(const QString& roleID)
{
    std::map<QString, ViewerToolButton*>::iterator found = _imp->toolButtons.find(roleID);

    if ( found == _imp->toolButtons.end() ) {
        return;
    }
    found->second->handleSelection();
}

void
NodeViewerContext::onToolActionTriggered()
{
    QAction* act = qobject_cast<QAction*>( sender() );

    if (act) {
        onToolActionTriggered(act);
    }
}

void
NodeViewerContext::onToolActionTriggered(QAction* act)
{
    _imp->onToolActionTriggeredInternal(act, true);
}

void
NodeViewerContext::setCurrentTool(const QString& toolID,
                                  bool notifyNode)
{
    QList<QAction*> actions;
    for (std::map<QString, ViewerToolButton*>::iterator it = _imp->toolButtons.begin(); it != _imp->toolButtons.end(); ++it) {
        QList<QAction*> roleActions = it->second->actions();
        actions.append(roleActions);
    }
    for (int i = 0; i < actions.size(); ++i) {
        QStringList actionData = actions[i]->data().toStringList();
        if (actionData.size() != 2) {
            continue;
        }
        if (actionData[1] == toolID) {
            _imp->onToolActionTriggeredInternal(actions[i], notifyNode);

            return;
        }
    }
}

void
NodeViewerContext::onToolGroupValueChanged(ViewSpec /*view*/,
                                           int /*dimension*/,
                                           int reason)
{
    KnobSignalSlotHandler* caller = dynamic_cast<KnobSignalSlotHandler*>( sender() );

    if (!caller) {
        return;
    }
    KnobIPtr knob = caller->getKnob();
    if (!knob) {
        return;
    }

    if ( (reason == eValueChangedReasonNatronGuiEdited) ||
         ( reason == eValueChangedReasonUserEdited) ) {
        return;
    }

    QString newRoleID = QString::fromUtf8( knob->getName().c_str() );
    std::map<QString, ViewerToolButton*>::iterator foundOldTool = _imp->toolButtons.find(newRoleID);
    assert( foundOldTool != _imp->toolButtons.end() );
    if ( foundOldTool == _imp->toolButtons.end() ) {
        return;
    }

    ViewerToolButton* newToolButton = foundOldTool->second;
    assert(newToolButton);
    _imp->toggleToolsSelection(newToolButton);
    newToolButton->setDown(true);

    _imp->currentRole = newRoleID;
}

void
NodeViewerContext::onToolActionValueChanged(ViewSpec /*view*/,
                                            int /*dimension*/,
                                            int reason)
{
    KnobSignalSlotHandler* caller = dynamic_cast<KnobSignalSlotHandler*>( sender() );

    if (!caller) {
        return;
    }
    KnobIPtr knob = caller->getKnob();
    if (!knob) {
        return;
    }

    if ( (reason == eValueChangedReasonNatronGuiEdited) ||
         ( reason == eValueChangedReasonUserEdited) ) {
        return;
    }

    QString newToolID = QString::fromUtf8( knob->getName().c_str() );
    std::map<QString, ViewerToolButton*>::iterator foundOldTool = _imp->toolButtons.find(_imp->currentRole);
    assert( foundOldTool != _imp->toolButtons.end() );
    if ( foundOldTool == _imp->toolButtons.end() ) {
        return;
    }

    ViewerToolButton* newToolButton = foundOldTool->second;
    assert(newToolButton);
    QList<QAction*> actions = newToolButton->actions();
    for (QList<QAction*>::iterator it = actions.begin(); it != actions.end(); ++it) {
        QStringList actionData = (*it)->data().toStringList();

        if (actionData.size() != 2) {
            continue;
        }
        const QString& actionRoleID = actionData[0];
        const QString& actionTool = actionData[1];
        assert(actionRoleID == _imp->currentRole);
        if ( (actionRoleID == _imp->currentRole) && (actionTool == newToolID) ) {
            newToolButton->setDefaultAction(*it);
            _imp->currentTool = newToolID;

            return;
        }
    }
}

void
NodeViewerContextPrivate::onToolActionTriggeredInternal(QAction* action,
                                                        bool notifyNode)
{
    if (!action) {
        return;
    }
    QStringList actionData = action->data().toStringList();

    if (actionData.size() != 2) {
        return;
    }


    const QString& newRoleID = actionData[0];
    const QString& newToolID = actionData[1];

    if (currentTool == newToolID && currentRole == newRoleID) {
        return;
    }

    std::map<QString, ViewerToolButton*>::iterator foundNextTool = toolButtons.find(newRoleID);
    assert( foundNextTool != toolButtons.end() );
    if ( foundNextTool == toolButtons.end() ) {
        return;
    }

    ViewerToolButton* newToolButton = foundNextTool->second;
    assert(newToolButton);
    toggleToolsSelection(newToolButton);
    newToolButton->setDown(true);
    newToolButton->setDefaultAction(action);

    QString oldRole = currentRole;
    QString oldTool = currentTool;

    currentRole = newRoleID;
    currentTool = newToolID;

    if (notifyNode) {
        // Refresh other viewers toolbars
        NodeGuiPtr n = node.lock();
        const std::list<ViewerTab*> viewers = publicInterface->getGui()->getViewersList();
        for (std::list<ViewerTab*>::const_iterator it = viewers.begin(); it != viewers.end(); ++it) {
            if (*it != viewerTab) {
                (*it)->updateSelectedToolForNode(newToolID, n);
            }
        }

        KnobIPtr oldGroupKnob = n->getNode()->getKnobByName( oldRole.toStdString() );
        KnobIPtr newGroupKnob = n->getNode()->getKnobByName( newRoleID.toStdString() );
        KnobIPtr oldToolKnob = n->getNode()->getKnobByName( oldTool.toStdString() );
        KnobIPtr newToolKnob = n->getNode()->getKnobByName( newToolID.toStdString() );
        assert(newToolKnob && newGroupKnob);
        if (newToolKnob && newGroupKnob) {

            KnobButtonPtr oldIsButton = oldToolKnob ? toKnobButton( oldToolKnob ) : KnobButtonPtr();
            KnobButtonPtr newIsButton = toKnobButton( newToolKnob );
            assert(newIsButton);

            KnobGroupPtr oldIsGroup = oldGroupKnob ? toKnobGroup( oldGroupKnob ) : KnobGroupPtr();
            KnobGroupPtr newIsGroup = toKnobGroup( newGroupKnob );
            assert(newIsGroup);
            if (newIsButton && newIsGroup) {
                EffectInstancePtr effect = n->getNode()->getEffectInstance();

                if (oldIsGroup) {
                    if (oldIsGroup->getValue() != false) {
                        oldIsGroup->onValueChanged(false, ViewSpec::all(), 0, eValueChangedReasonUserEdited, 0);
                    } else {
                        // We must issue at least a knobChanged call
                        effect->onKnobValueChanged_public(oldIsGroup, eValueChangedReasonUserEdited, effect->getCurrentTime(), ViewSpec(0), true);
                    }
                }
                
                if (newIsGroup->getValue() != true) {
                    newIsGroup->onValueChanged(true, ViewSpec::all(), 0, eValueChangedReasonUserEdited, 0);
                } else {
                    // We must issue at least a knobChanged call
                    effect->onKnobValueChanged_public(newIsGroup, eValueChangedReasonUserEdited, effect->getCurrentTime(), ViewSpec(0), true);
                }


                // Only change the value of the button if we are in the same group
                if (oldIsButton && oldIsGroup == newIsGroup) {
                    if (oldIsButton->getValue() != false) {
                        oldIsButton->onValueChanged(false, ViewSpec::all(), 0, eValueChangedReasonUserEdited, 0);
                    } else {
                        // We must issue at least a knobChanged call
                        effect->onKnobValueChanged_public(oldIsButton, eValueChangedReasonUserEdited, effect->getCurrentTime(), ViewSpec(0), true);
                    }
                }
                if (newIsButton->getValue() != true) {
                    newIsButton->onValueChanged(true, ViewSpec::all(), 0, eValueChangedReasonUserEdited, 0);
                } else {
                    // We must issue at least a knobChanged call
                    effect->onKnobValueChanged_public(newIsButton, eValueChangedReasonUserEdited, effect->getCurrentTime(), ViewSpec(0), true);
                }
            }
        }
    }
} // NodeViewerContextPrivate::onToolActionTriggeredInternal

void
NodeViewerContext::updateSelectionFromViewerSelectionRectangle(bool onRelease)
{
    NodeGuiPtr n = _imp->getNode();

    if (!n) {
        return;
    }
    NodePtr node = n->getNode();
    if ( !node || !node->isActivated() ) {
        return;
    }
    RectD rect;
    _imp->viewer->getSelectionRectangle(rect.x1, rect.x2, rect.y1, rect.y2);
    node->getEffectInstance()->onInteractViewportSelectionUpdated(rect, onRelease);
}

void
NodeViewerContext::onViewerSelectionCleared()
{
    NodeGuiPtr n = _imp->getNode();

    if (!n) {
        return;
    }
    NodePtr node = n->getNode();
    if ( !node || !node->isActivated() ) {
        return;
    }
    node->getEffectInstance()->onInteractViewportSelectionCleared();
}

void
NodeViewerContext::notifyGuiClosing()
{
    _imp->viewer = 0;
    _imp->viewerTab = 0;
}

NATRON_NAMESPACE_EXIT;
NATRON_NAMESPACE_USING;
#include "moc_NodeViewerContext.cpp"
