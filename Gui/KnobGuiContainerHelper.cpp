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

#include "KnobGuiContainerHelper.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QScrollArea>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QApplication>
#include <QUndoStack>
#include <QUndoCommand>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Engine/EffectInstance.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"

#include "Gui/KnobGui.h"
#include "Gui/ClickableLabel.h"
#include "Gui/KnobGuiGroup.h"
#include "Gui/NodeGui.h"
#include "Gui/Gui.h"
#include "Gui/GuiDefines.h"
#include "Gui/GuiApplicationManager.h"
#include "Gui/TabGroup.h"

#define NATRON_FORM_LAYOUT_LINES_SPACING 0
#define NATRON_SETTINGS_VERTICAL_SPACING_PIXELS 3


NATRON_NAMESPACE_ENTER;

struct KnobGuiContainerHelperPrivate
{
    KnobGuiContainerHelper* _p;
    KnobHolderWPtr holder;

    // Stores our KnobGui refered to by the internal Knob
    KnobsGuiMapping knobsMap;
    KnobPageGuiWPtr currentPage;
    PagesMap pages;
    boost::shared_ptr<QUndoStack> undoStack; /*!< undo/redo stack*/
    QUndoCommand* cmdBeingPushed;
    bool clearedStackDuringPush;
    boost::scoped_ptr<KnobGuiContainerSignalsHandler> signals;

    KnobGuiContainerHelperPrivate(KnobGuiContainerHelper* p,
                                  const KnobHolderPtr& holder,
                                  const boost::shared_ptr<QUndoStack>& stack)
        : _p(p)
        , holder(holder)
        , knobsMap()
        , currentPage()
        , pages()
        , undoStack()
        , cmdBeingPushed(0)
        , clearedStackDuringPush(false)
        , signals( new KnobGuiContainerSignalsHandler(p) )
    {
        if (stack) {
            undoStack = stack;
        } else {
            undoStack.reset(new QUndoStack);
        }
    }

    KnobGuiPtr createKnobGui(const KnobIPtr &knob);

    KnobsGuiMapping::iterator findKnobGui(const KnobIPtr& knob);

    void refreshPagesEnabledness();
};

KnobGuiContainerHelper::KnobGuiContainerHelper(const KnobHolderPtr& holder,
                                               const boost::shared_ptr<QUndoStack>& stack)
    : KnobGuiContainerI()
    , DockablePanelI()
    , _imp( new KnobGuiContainerHelperPrivate(this, holder, stack) )
{
}

KnobGuiContainerHelper::~KnobGuiContainerHelper()
{
    ///Delete the knob gui if they weren't before
    ///normally the onKnobDeletion() function should have cleared them
    for (KnobsGuiMapping::const_iterator it = _imp->knobsMap.begin(); it != _imp->knobsMap.end(); ++it) {
        if (it->second) {
            KnobIPtr knob = it->first.lock();
            it->second->setGuiRemoved();
        }
    }
}

const PagesMap&
KnobGuiContainerHelper::getPages() const
{
    return _imp->pages;
}

KnobGuiPtr
KnobGuiContainerHelper::getKnobGui(const KnobIPtr& knob) const
{
    for (KnobsGuiMapping::const_iterator it = _imp->knobsMap.begin(); it != _imp->knobsMap.end(); ++it) {
        if (it->first.lock() == knob) {
            return it->second;
        }
    }

    return KnobGuiPtr();
}

int
KnobGuiContainerHelper::getItemsSpacingOnSameLine() const
{
    return TO_DPIX(15);
}

KnobGuiPtr
KnobGuiContainerHelperPrivate::createKnobGui(const KnobIPtr &knob)
{
    KnobsGuiMapping::iterator found = findKnobGui(knob);

    if ( found != knobsMap.end() ) {
        return found->second;
    }

    KnobGuiPtr ret( appPTR->createGuiForKnob(knob, _p) );
    assert(ret);
    if (!ret) {
        return ret;
    }
    ret->initialize();
    knobsMap.push_back( std::make_pair(knob, ret) );

    return ret;
}

KnobsGuiMapping::iterator
KnobGuiContainerHelperPrivate::findKnobGui(const KnobIPtr& knob)
{
    for (KnobsGuiMapping::iterator it = knobsMap.begin(); it != knobsMap.end(); ++it) {
        if (it->first.lock() == knob) {
            return it;
        }
    }

    return knobsMap.end();
}

KnobPageGuiPtr
KnobGuiContainerHelper::getOrCreateDefaultPage()
{
    const std::vector< KnobIPtr > & knobs = getInternalKnobs();

    // Find in all knobs a page param to set this param into
    std::list<KnobPagePtr > pagesNotDeclaredByPlugin;
    for (U32 i = 0; i < knobs.size(); ++i) {
        KnobPagePtr p = toKnobPage( knobs[i]);
        if (p) {
            if (p->isDeclaredByPlugin()) {
                return getOrCreatePage(p);
            } else {
                pagesNotDeclaredByPlugin.push_back(p);
            }
        }
    }
    if (!pagesNotDeclaredByPlugin.empty()) {
        return getOrCreatePage(pagesNotDeclaredByPlugin.front());
    }
    // The plug-in didn't specify any page, it should have been caught before in Node::getOrCreateMainPage
    assert(false);

    return KnobPageGuiPtr();
}

KnobPageGuiPtr
KnobGuiContainerHelper::getCurrentPage() const
{
    return _imp->currentPage.lock();
}

void
KnobGuiContainerHelper::setCurrentPage(const KnobPageGuiPtr& curPage)
{
    _imp->currentPage = curPage;
    _imp->refreshPagesEnabledness();
}

KnobPageGuiPtr
KnobGuiContainerHelper::getOrCreatePage(const KnobPagePtr& page)
{

    if (!page || page->getIsToolBar()) {
        return KnobPageGuiPtr();
    }
    if ( !isPagingEnabled() && (_imp->pages.size() > 0) ) {
        return _imp->pages.begin()->second;
    }

    // If the page is already created, return it
    assert(page);
    PagesMap::iterator found = _imp->pages.find(page);
    if ( found != _imp->pages.end() ) {
        return found->second;
    }


    QWidget* newTab;
    QWidget* layoutContainer;

    // The widget parent of the page
    QWidget* pagesContainer = getPagesContainer();
    assert(pagesContainer);

    // Check If the page main widget should be a scrollarea
    if ( useScrollAreaForTabs() ) {
        QScrollArea* sa = new QScrollArea(pagesContainer);
        layoutContainer = new QWidget(sa);
        layoutContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sa->setWidgetResizable(true);
        sa->setWidget(layoutContainer);
        newTab = sa;
    } else {
        // Otherwise let the derived class create the main widget
        newTab = createPageMainWidget(pagesContainer);
        layoutContainer = newTab;
    }

    // The container layout is always a grid layout
    QGridLayout *tabLayout = new QGridLayout(layoutContainer);
    tabLayout->setObjectName( QString::fromUtf8("formLayout") );
    layoutContainer->setLayout(tabLayout);
    tabLayout->setColumnStretch(1, 1);
    tabLayout->setSpacing( TO_DPIY(NATRON_FORM_LAYOUT_LINES_SPACING) );

    // Create the page gui
    KnobPageGuiPtr pageGui( new KnobPageGui() );
    pageGui->currentRow = 0;
    pageGui->tab = newTab;
    pageGui->pageKnob = page;
    pageGui->groupAsTab = 0;
    pageGui->gridLayout = tabLayout;

    boost::shared_ptr<KnobSignalSlotHandler> handler = page->getSignalSlotHandler();
    QObject::connect( handler.get(), SIGNAL(labelChanged()), _imp->signals.get(), SLOT(onPageLabelChangedInternally()) );
    QObject::connect( handler.get(), SIGNAL(secretChanged()), _imp->signals.get(), SLOT(onPageSecretnessChanged()) );

    // Add the page to the container (most likely a tab widget)
    if (!page->getIsSecret()) {
        addPageToPagesContainer(pageGui);
    } else {
        newTab->hide();
    }

    pageGui->tab->setToolTip( QString::fromUtf8( page->getHintToolTip().c_str() ) );

    _imp->pages.insert( std::make_pair(page, pageGui) );

    return pageGui;
} // KnobGuiContainerHelper::getOrCreatePage

static QPixmap
getStandardIcon(QMessageBox::Icon icon,
                int size,
                QWidget* widget)
{
    QStyle *style = widget ? widget->style() : QApplication::style();
    QIcon tmpIcon;

    switch (icon) {
    case QMessageBox::Information:
        tmpIcon = style->standardIcon(QStyle::SP_MessageBoxInformation, 0, widget);
        break;
    case QMessageBox::Warning:
        tmpIcon = style->standardIcon(QStyle::SP_MessageBoxWarning, 0, widget);
        break;
    case QMessageBox::Critical:
        tmpIcon = style->standardIcon(QStyle::SP_MessageBoxCritical, 0, widget);
        break;
    case QMessageBox::Question:
        tmpIcon = style->standardIcon(QStyle::SP_MessageBoxQuestion, 0, widget);
    default:
        break;
    }
    if ( !tmpIcon.isNull() ) {
        return tmpIcon.pixmap(size, size);
    }

    return QPixmap();
}

/**
 * @brief Given a knob "ref" within a vector of knobs, returns a vector
 * of all knobs that should be on the same horizontal line as this knob.
 **/
static void
findKnobsOnSameLine(const KnobsVec& knobs,
                    const KnobIPtr& ref,
                    KnobsVec& knobsOnSameLine)
{
    int idx = -1;

    for (U32 k = 0; k < knobs.size(); ++k) {
        if (knobs[k] == ref) {
            idx = k;
            break;
        }
    }
    assert(idx != -1);
    if (idx < 0) {
        return;
    }
    ///find all knobs backward that are on the same line.
    int k = idx - 1;
    KnobIPtr parent = ref->getParentKnob();

    while ( k >= 0 && !knobs[k]->isNewLineActivated() ) {
        if (parent) {
            assert(knobs[k]->getParentKnob() == parent);
            knobsOnSameLine.push_back(knobs[k]);
        } else {
            if ( !knobs[k]->getParentKnob() &&
                 !toKnobPage( knobs[k] ) &&
                 !toKnobGroup( knobs[k] ) ) {
                knobsOnSameLine.push_back(knobs[k]);
            }
        }
        --k;
    }

    ///find all knobs forward that are on the same line.
    k = idx;
    while ( k < (int)(knobs.size() - 1) && !knobs[k]->isNewLineActivated() ) {
        if (parent) {
            assert(knobs[k + 1]->getParentKnob() == parent);
            knobsOnSameLine.push_back(knobs[k + 1]);
        } else {
            if ( !knobs[k + 1]->getParentKnob() &&
                 !toKnobPage( knobs[k + 1] ) &&
                 !toKnobGroup( knobs[k + 1] ) ) {
                knobsOnSameLine.push_back(knobs[k + 1]);
            }
        }
        ++k;
    }
}

const KnobsVec&
KnobGuiContainerHelper::getInternalKnobs() const
{
    return _imp->holder.lock()->getKnobs();
}

const KnobsGuiMapping&
KnobGuiContainerHelper::getKnobsMapping() const
{
    return _imp->knobsMap;
}

void
KnobGuiContainerHelper::initializeKnobs()
{
    initializeKnobVector( _imp->holder.lock()->getKnobs() );
    _imp->refreshPagesEnabledness();
    refreshCurrentPage();

    onKnobsInitialized();
}

void
KnobGuiContainerHelper::initializeKnobVectorInternal(const KnobsVec& siblingsVec,
                                                     KnobsVec* regularKnobsVec)
{
    // A pointer to the container of the last knob created on the same row
    QWidget* lastRowWidget = 0;

    // Hold a pointer to the previous child if the previous child did not want to create a new line
    KnobsVec::const_iterator prev = siblingsVec.end();

    for (KnobsVec::const_iterator it2 = siblingsVec.begin(); it2 != siblingsVec.end(); ++it2) {
        bool makeNewLine = true;
        int lastKnobSpacing = 0;
        KnobGroupPtr isGroup = toKnobGroup(*it2);

        // A vector of all other knobs on the same line
        KnobsVec knobsOnSameLine;

        // If the knob is dynamic (i:e created after the initial creation of knobs)
        // it can be added as part of a group defined earlier hence we have to insert it at the proper index.
        KnobIPtr parentKnob = (*it2)->getParentKnob();
        KnobGroupPtr isParentGroup = toKnobGroup(parentKnob);

        // Determine if we should create this knob on a new line or use the one created before
        if (!isGroup) {
            if ( ( prev != siblingsVec.end() ) && !(*prev)->isNewLineActivated() ) {
                makeNewLine = false;
                lastKnobSpacing = (*prev)->getSpacingBetweenitems();
            }
            if (isParentGroup) {
                // If the parent knob is a group, knobs on the same line have to be found in the children
                // of the parent
                KnobsVec groupsiblings = isParentGroup->getChildren();
                findKnobsOnSameLine(groupsiblings, *it2, knobsOnSameLine);
            } else {
                // Parent is a page, find the siblings in the children of the page
                findKnobsOnSameLine(siblingsVec, *it2, knobsOnSameLine);
            }
        }

        // Create this knob
        KnobGuiPtr newGui = findKnobGuiOrCreate(*it2, makeNewLine, lastKnobSpacing, lastRowWidget, knobsOnSameLine);

        // Childrens cannot be on the same row than their parent
        if (!isGroup && newGui) {
            lastRowWidget = newGui->getFieldContainer();
        }


        if ( prev == siblingsVec.end() ) {
            prev = siblingsVec.begin();
        } else {
            ++prev;
        }

        // Remove it from the "regularKnobs" to mark it as created as we will use them later
        if (regularKnobsVec) {
            KnobsVec::iterator foundRegular = std::find(regularKnobsVec->begin(), regularKnobsVec->end(), *it2);
            if ( foundRegular != regularKnobsVec->end() ) {
                regularKnobsVec->erase(foundRegular);
            }
        }
    }
} // KnobGuiContainerHelper::initializeKnobVectorInternal

void
KnobGuiContainerHelper::initializeKnobVector(const KnobsVec& knobs)
{
    // Extract pages first and initialize them recursively
    // All other knobs are put in regularKnobs
    std::list<KnobPagePtr > pages;
    KnobsVec regularKnobs;

    for (std::size_t i = 0; i < knobs.size(); ++i) {
        KnobPagePtr isPage = toKnobPage(knobs[i]);
        if (isPage) {
            if (!isPage->getIsToolBar()) {
                pages.push_back(isPage);
            }
        } else {
            regularKnobs.push_back(knobs[i]);
        }
    }
    for (std::list<KnobPagePtr >::iterator it = pages.begin(); it != pages.end(); ++it) {
        // Create page
        KnobGuiPtr knobGui = findKnobGuiOrCreate( *it, true /*makeNewLine*/, 0/*lastKnobSpacing*/,  0 /*lastRowWidget*/, KnobsVec() /*knobsOnSameLine*/);
        Q_UNUSED(knobGui);

        // Create its children
        KnobsVec children = (*it)->getChildren();
        initializeKnobVectorInternal(children, &regularKnobs);
    }

    // For knobs that did not belong to a page,  create them
    initializeKnobVectorInternal(regularKnobs, 0);
    refreshTabWidgetMaxHeight();
}

static void
workAroundGridLayoutBug(QGridLayout* layout)
{
    // See http://stackoverflow.com/questions/14033902/qt-qgridlayout-automatically-centers-moves-items-to-the-middle for
    // a bug of QGridLayout: basically all items are centered, but we would like to add stretch in the bottom of the layout.
    // To do this we add an empty widget with an expanding vertical size policy.
    QWidget* foundSpacer = 0;

    for (int i = 0; i < layout->rowCount(); ++i) {
        QLayoutItem* item = layout->itemAtPosition(i, 0);
        if (!item) {
            continue;
        }
        QWidget* w = item->widget();
        if (!w) {
            continue;
        }
        if ( w->objectName() == QString::fromUtf8("emptyWidget") ) {
            foundSpacer = w;
            break;
        }
    }
    if (foundSpacer) {
        layout->removeWidget(foundSpacer);
    } else {
        foundSpacer = new QWidget( layout->parentWidget() );
        foundSpacer->setObjectName( QString::fromUtf8("emptyWidget") );
        foundSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    ///And add our stretch
    layout->addWidget(foundSpacer, layout->rowCount(), 0, 1, 2);
}

void
KnobGuiContainerHelper::setLabelFromTextAndIcon(KnobClickableLabel* widget, const QString& labelText, const QString& labelIconFilePath, bool setBold)
{
    bool pixmapSet = false;
    if ( !labelIconFilePath.isEmpty() ) {
        QPixmap pix;

        //QFontMetrics fm(widget->font(), 0);
        int pixSize = TO_DPIY(NATRON_MEDIUM_BUTTON_ICON_SIZE);

        if (labelIconFilePath == QLatin1String("dialog-warning")) {
            pix = getStandardIcon(QMessageBox::Warning, pixSize, widget);
        } else if (labelIconFilePath == QLatin1String("dialog-question")) {
            pix = getStandardIcon(QMessageBox::Question, pixSize, widget);
        } else if (labelIconFilePath == QLatin1String("dialog-error")) {
            pix = getStandardIcon(QMessageBox::Critical, pixSize, widget);
        } else if (labelIconFilePath == QLatin1String("dialog-information")) {
            pix = getStandardIcon(QMessageBox::Information, pixSize, widget);
        } else {
            pix.load(labelIconFilePath);
            if (pix.width() != pixSize) {
                pix = pix.scaled(pixSize, pixSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }
        if ( !pix.isNull() ) {
            pixmapSet = true;
            widget->setPixmap(pix);
        }
    }
    if (!pixmapSet) {
        /*labelStr += ":";*/
        if ( setBold ) {
            widget->setBold(true);
        }
        widget->setText_overload(labelText );
    }

}

KnobGuiPtr
KnobGuiContainerHelper::findKnobGuiOrCreate(const KnobIPtr & knob,
                                            bool makeNewLine,
                                            int lastKnobLineSpacing,
                                            QWidget* lastRowWidget,
                                            const KnobsVec& knobsOnSameLine)
{
    assert(knob);

    // Groups and Pages have special cases in the following code as they are containers
    KnobGroupPtr isGroup = toKnobGroup(knob);
    KnobPagePtr isPage = toKnobPage(knob);

    // Is this knob already described in the gui ?
    for (KnobsGuiMapping::const_iterator it = _imp->knobsMap.begin(); it != _imp->knobsMap.end(); ++it) {
        if ( (it->first.lock() == knob) && it->second ) {
            if (isPage) {
                return it->second;
            } else if ( isGroup && ( ( !isGroup->isTab() && it->second->hasWidgetBeenCreated() ) || isGroup->isTab() ) ) {
                return it->second;
            } else if ( it->second->hasWidgetBeenCreated() ) {
                return it->second;
            } else {
                break;
            }
        }
    }


    // For a page, create it if needed and recursively describe its children
    if (isPage) {
        if ( isPage->getChildren().empty() ) {
            return KnobGuiPtr();
        }
        getOrCreatePage(isPage);
        KnobsVec children = isPage->getChildren();
        initializeKnobVector(children);

        return KnobGuiPtr();
    }

    // Create the actual knob gui object
    KnobGuiPtr ret = _imp->createKnobGui(knob);
    if (!ret) {
        return KnobGuiPtr();
    }

    KnobIPtr parentKnob = knob->getParentKnob();

    // If this assert triggers, that means a knob was not added to a KnobPage.
    assert(parentKnob || !isPagingEnabled());

    KnobGroupPtr parentIsGroup = toKnobGroup(parentKnob);
    KnobGuiGroup* parentGui = 0;

    // If this knob is within a group, make sure the group is created so far
    if (parentIsGroup) {
        parentGui = dynamic_cast<KnobGuiGroup*>( findKnobGuiOrCreate( parentKnob, true, 0, 0, KnobsVec() ).get() );
    }

    // So far the knob could have no parent, in which case we force it to be in the default page.
    if (!parentKnob) {
        KnobPageGuiPtr defPage = getOrCreateDefaultPage();
        defPage->pageKnob.lock()->addKnob(knob);
        parentKnob = defPage->pageKnob.lock();
    }

    assert(parentKnob);

    // For group only create the widgets if it is not a tab, otherwise do a special case
    if ( isGroup  && isGroup->isTab() ) {
        KnobPagePtr parentIsPage = toKnobPage(parentKnob);
        if (!parentKnob || parentIsPage) {
            KnobPageGuiPtr page = getOrCreatePage(parentIsPage);

            // Create the frame for the group that are set as tabs within this tab
            bool existed = true;
            if (!page->groupAsTab) {
                existed = false;
                page->groupAsTab = new TabGroup( getPagesContainer() );
            }
            page->groupAsTab->addTab( isGroup, QString::fromUtf8( isGroup->getLabel().c_str() ) );
            if (!existed) {
                page->gridLayout->addWidget(page->groupAsTab, page->currentRow, 0, 1, 2);
            }

            page->groupAsTab->refreshTabSecretNess( isGroup );
        } else {
            // This is a group inside a group
            assert(parentIsGroup);
            assert(parentGui);
            TabGroup* groupAsTab = parentGui->getOrCreateTabWidget();
            assert(groupAsTab);
            groupAsTab->addTab( isGroup, QString::fromUtf8( isGroup->getLabel().c_str() ) );

            if ( parentIsGroup && parentIsGroup->isTab() ) {
                // Insert the tab in the layout of the parent
                // Find the page in the parentParent group
                KnobIPtr parentParent = parentKnob->getParentKnob();
                assert(parentParent);
                KnobGroupPtr parentParentIsGroup = toKnobGroup(parentParent);
                KnobPagePtr parentParentIsPage = toKnobPage(parentParent);
                assert(parentParentIsGroup || parentParentIsPage);
                TabGroup* parentTabGroup = 0;
                if (parentParentIsPage) {
                    KnobPageGuiPtr page = getOrCreatePage(parentParentIsPage);
                    assert(page);
                    parentTabGroup = page->groupAsTab;
                } else {
                    KnobsGuiMapping::iterator it = _imp->findKnobGui(parentParent);
                    assert( it != _imp->knobsMap.end() );
                    KnobGuiGroup* parentParentGroupGui = dynamic_cast<KnobGuiGroup*>( it->second.get() );
                    assert(parentParentGroupGui);
                    parentTabGroup = parentParentGroupGui->getOrCreateTabWidget();
                }

                QGridLayout* layout = parentTabGroup->addTab( parentIsGroup, QString::fromUtf8( parentIsGroup->getLabel().c_str() ) );
                assert(layout);
                layout->addWidget(groupAsTab, 0, 0, 1, 2);
            } else {
                KnobPagePtr topLevelPage = knob->getTopLevelPage();
                KnobPageGuiPtr page = getOrCreatePage(topLevelPage);
                assert(page);
                page->gridLayout->addWidget(groupAsTab, page->currentRow, 0, 1, 2);
            }
            groupAsTab->refreshTabSecretNess(isGroup);
        }
    }
    // If widgets for the KnobGui have already been created, don't do the following
    else if ( !ret->hasWidgetBeenCreated() ) {
        // Get the top level parent
        KnobPagePtr isTopLevelParentAPage = toKnobPage(parentKnob);
        KnobIPtr parentKnobTmp = parentKnob;
        while (parentKnobTmp && !isTopLevelParentAPage) {
            parentKnobTmp = parentKnobTmp->getParentKnob();
            if (parentKnobTmp) {
                isTopLevelParentAPage = toKnobPage(parentKnobTmp);
            }
        }

        // Find in which page the knob should be
        assert(isTopLevelParentAPage);

        KnobPageGuiPtr page = getOrCreatePage(isTopLevelParentAPage);
        if (!page) {
            return ret;
        }
        ///retrieve the form layout
        QGridLayout* layout = page->gridLayout;

        // If the knob has specified that it didn't want to trigger a new line, decrement the current row
        // index of the tab
        if (!makeNewLine) {
            --page->currentRow;
        }

        QWidget* fieldContainer = 0;
        QHBoxLayout* fieldLayout = 0;

        if (makeNewLine) {
            ///if new line is not turned off, create a new line
            fieldContainer = createKnobHorizontalFieldContainer(page->tab);
            fieldLayout = new QHBoxLayout(fieldContainer);
            fieldLayout->setContentsMargins( TO_DPIX(3), 0, 0, TO_DPIY(NATRON_SETTINGS_VERTICAL_SPACING_PIXELS) );
            fieldLayout->setSpacing( TO_DPIY(2) );
            fieldLayout->setAlignment(Qt::AlignLeft);
        } else {
            ///otherwise re-use the last row's widget and layout
            assert(lastRowWidget);
            fieldContainer = lastRowWidget;
            fieldLayout = dynamic_cast<QHBoxLayout*>( fieldContainer->layout() );
        }

        assert(fieldContainer);
        assert(fieldLayout);

        ///Create the label if needed
        KnobClickableLabel* label = 0;
        Label* warningLabel = 0;
        std::string descriptionLabel = ret->getDescriptionLabel();
        const std::string& labelIconFilePath = knob->getIconLabel();
        QWidget *labelContainer = 0;
        QHBoxLayout *labelLayout = 0;
        const bool hasLabel = ret->shouldCreateLabel();
        if (hasLabel) {
            if (makeNewLine) {
                labelContainer = new QWidget(page->tab);
                labelLayout = new QHBoxLayout(labelContainer);
                labelLayout->setContentsMargins( TO_DPIX(3), 0, 0, TO_DPIY(NATRON_SETTINGS_VERTICAL_SPACING_PIXELS) );
                labelLayout->setSpacing( TO_DPIY(2) );
            }

            label = new KnobClickableLabel(QString(), ret, page->tab);
            warningLabel = new Label(page->tab);
            warningLabel->setVisible(false);
            QFontMetrics fm(label->font(), 0);
            int pixSize = fm.height();
            QPixmap stdErrorPix;
            stdErrorPix = getStandardIcon(QMessageBox::Critical, pixSize, label);
            warningLabel->setPixmap(stdErrorPix);

            setLabelFromTextAndIcon(label, QString::fromUtf8(descriptionLabel.c_str()), QString::fromUtf8(labelIconFilePath.c_str()), ret->isLabelBold());
            QObject::connect( label, SIGNAL(clicked(bool)), ret.get(), SIGNAL(labelClicked(bool)) );


            if (makeNewLine) {
                labelLayout->addWidget(warningLabel);
                labelLayout->addWidget(label);
            }
        } // hasLabel

        /*
         * Find out in which layout the knob should be: either in the layout of the page or in the layout of
         * the nearest parent group tab in the hierarchy
         */
        KnobGroupPtr closestParentGroupTab;
        KnobIPtr parentTmp = parentKnob;
        assert(parentKnobTmp);
        while (!closestParentGroupTab) {
            KnobGroupPtr parentGroup = toKnobGroup(parentTmp);
            if ( parentGroup && parentGroup->isTab() ) {
                closestParentGroupTab = parentGroup;
            }
            parentTmp = parentTmp->getParentKnob();
            if (!parentTmp) {
                break;
            }
        }

        if (closestParentGroupTab) {
            /*
             * At this point we know that the parent group (which is a tab in the TabWidget) will have at least 1 knob
             * so ensure it is added to the TabWidget.
             * There are 2 possibilities, either the parent of the group tab is another group, in which case we have to
             * make sure the TabWidget is visible in the parent TabWidget of the group, otherwise we just add the TabWidget
             * to the on of the page.
             */

            KnobIPtr parentParent = closestParentGroupTab->getParentKnob();
            KnobGroupPtr parentParentIsGroup = toKnobGroup(parentParent);
            KnobPagePtr parentParentIsPage = toKnobPage(parentParent);

            assert(parentParentIsGroup || parentParentIsPage);
            if (parentParentIsGroup) {
                KnobGuiGroup* parentParentGroupGui = dynamic_cast<KnobGuiGroup*>( findKnobGuiOrCreate( parentParent, true, 0,  0, KnobsVec() ).get() );
                assert(parentParentGroupGui);
                if (parentParentGroupGui) {
                    TabGroup* groupAsTab = parentParentGroupGui->getOrCreateTabWidget();
                    assert(groupAsTab);
                    layout = groupAsTab->addTab( closestParentGroupTab, QString::fromUtf8( closestParentGroupTab->getLabel().c_str() ) );
                }
            } else if (parentParentIsPage) {
                KnobPageGuiPtr page = getOrCreatePage(parentParentIsPage);
                assert(page);
                assert(page->groupAsTab);
                layout = page->groupAsTab->addTab( closestParentGroupTab, QString::fromUtf8( closestParentGroupTab->getLabel().c_str() ) );
            }
            assert(layout);
        }

        // Fill the fieldLayout with the widgets
        ret->createGUI(fieldContainer, labelContainer, label, warningLabel, fieldLayout, makeNewLine, lastKnobLineSpacing, knobsOnSameLine);

        ret->setEnabledSlot();

        // Must add the row to the layout before calling setSecret()
        if (makeNewLine) {
            int rowIndex;
            if (closestParentGroupTab) {
                rowIndex = layout->rowCount();
            } else if ( parentGui && knob->isDynamicallyCreated() ) {
                const std::list<KnobGuiWPtr>& children = parentGui->getChildren();
                if ( children.empty() ) {
                    rowIndex = parentGui->getActualIndexInLayout();
                } else {
                    rowIndex = children.back().lock()->getActualIndexInLayout();
                }
                ++rowIndex;
            } else {
                rowIndex = page->currentRow;
            }


            const bool labelOnSameColumn = ret->isLabelOnSameColumn();
            Qt::Alignment labelAlignment;
            Qt::Alignment fieldAlignment;

            if (isGroup) {
                labelAlignment = Qt::AlignLeft;
            } else {
                labelAlignment = Qt::AlignRight;
            }

            if (!hasLabel) {
                layout->addWidget(fieldContainer, rowIndex, 0, 1, 2);
            } else {
                if (label) {
                    if (labelOnSameColumn) {
                        labelLayout->addWidget(fieldContainer);
                        layout->addWidget(labelContainer, rowIndex, 0, 1, 2);
                    } else {
                        layout->addWidget(labelContainer, rowIndex, 0, 1, 1, labelAlignment);
                        layout->addWidget(fieldContainer, rowIndex, 1, 1, 1);
                    }
                }
            }

            workAroundGridLayoutBug(layout);
        } // makeNewLine

        ret->setSecret();

        if ( knob->isNewLineActivated() && ret->shouldAddStretch() ) {
            fieldLayout->addStretch();
        }


        ///increment the row count
        ++page->currentRow;

        if (parentIsGroup && parentGui) {
            parentGui->addKnob(ret);
        }
    } //  if ( !ret->hasWidgetBeenCreated() && ( !isGroup || !isGroup->isTab() ) ) {


    // If the knob is a group, create all the children
    if (isGroup) {
        KnobsVec children = isGroup->getChildren();
        initializeKnobVector(children);
    }

    return ret;
} // findKnobGuiOrCreate

QWidget*
KnobGuiContainerHelper::createKnobHorizontalFieldContainer(QWidget* parent) const
{
    return new QWidget(parent);
}

void
KnobGuiContainerHelper::deleteKnobGui(const KnobIPtr& knob)
{
    KnobPagePtr isPage = toKnobPage(knob);

    if ( isPage && isPagingEnabled() ) {
        // Remove the page and all its children
        PagesMap::iterator found = _imp->pages.find(isPage);
        if ( found != _imp->pages.end() ) {
            _imp->refreshPagesEnabledness();

            KnobsVec children = isPage->getChildren();
            for (U32 i = 0; i < children.size(); ++i) {
                deleteKnobGui(children[i]);
            }

            found->second->tab->deleteLater();
            found->second->currentRow = 0;
            _imp->pages.erase(found);
        }
    } else {
        // This is not a page or paging is disabled

        KnobGroupPtr isGrp = toKnobGroup(knob);
        if (isGrp) {
            KnobsVec children = isGrp->getChildren();
            for (U32 i = 0; i < children.size(); ++i) {
                deleteKnobGui(children[i]);
            }
        }
        if ( isGrp && isGrp->isTab() ) {
            //find parent page
            KnobIPtr parent = knob->getParentKnob();
            KnobPagePtr isParentPage = toKnobPage(parent);
            KnobGroupPtr isParentGroup = toKnobGroup(parent);

            assert(isParentPage || isParentGroup);
            if (isParentPage) {
                PagesMap::iterator page = _imp->pages.find(isParentPage);
                assert( page != _imp->pages.end() );
                TabGroup* groupAsTab = page->second->groupAsTab;
                if (groupAsTab) {
                    groupAsTab->removeTab(isGrp);
                    if ( groupAsTab->isEmpty() ) {
                        delete page->second->groupAsTab;
                        page->second->groupAsTab = 0;
                    }
                }
            } else if (isParentGroup) {
                KnobsGuiMapping::iterator found  = _imp->findKnobGui(knob);
                assert( found != _imp->knobsMap.end() );
                KnobGuiGroup* parentGroupGui = dynamic_cast<KnobGuiGroup*>( found->second.get() );
                assert(parentGroupGui);
                if (parentGroupGui) {
                    TabGroup* groupAsTab = parentGroupGui->getOrCreateTabWidget();
                    if (groupAsTab) {
                        groupAsTab->removeTab(isGrp);
                        if ( groupAsTab->isEmpty() ) {
                            parentGroupGui->removeTabWidget();
                        }
                    }
                }
            }

            KnobsGuiMapping::iterator it  = _imp->findKnobGui(knob);
            if ( it != _imp->knobsMap.end() ) {
                _imp->knobsMap.erase(it);
            }
        } else {
            KnobsGuiMapping::iterator it  = _imp->findKnobGui(knob);
            if ( it != _imp->knobsMap.end() ) {
                it->second->removeGui();
                _imp->knobsMap.erase(it);
            }
        }
    }
} // KnobGuiContainerHelper::deleteKnobGui

void
KnobGuiContainerHelper::refreshGuiForKnobsChanges(bool restorePageIndex)
{
    KnobPageGuiPtr curPage;

    if ( isPagingEnabled() ) {
        if (restorePageIndex) {
            curPage = getCurrentPage();
        }
    }

    // Delete all knobs gui
    {
        KnobsGuiMapping mapping = _imp->knobsMap;
        _imp->knobsMap.clear();
        for (KnobsGuiMapping::iterator it = mapping.begin(); it != mapping.end(); ++it) {
            assert(it->second);
            KnobIPtr knob = it->first.lock();
            if (knob) {
                knob->setKnobGuiPointer( KnobGuiPtr() );
            }
            it->second->removeGui();
            it->second.reset();
        }
    }

    // Now delete all pages
    for (PagesMap::iterator it = _imp->pages.begin(); it != _imp->pages.end(); ++it) {
        removePageFromContainer(it->second);
        it->second->tab->deleteLater();
        it->second->currentRow = 0;
    }
    _imp->pages.clear();

    //Clear undo/Redo stack so that KnobGui pointers are not lying around
    clearUndoRedoStack();

    recreateKnobsInternal(curPage, restorePageIndex);

    _imp->refreshPagesEnabledness();

}

void
KnobGuiContainerHelper::recreateViewerUIKnobs()
{
    Gui* gui = getGui();
    if (!gui) {
        return;
    }
    EffectInstancePtr isEffect = toEffectInstance(_imp->holder.lock());
    if (!isEffect) {
        return;
    }

    NodePtr thisNode = isEffect->getNode();
    if (!thisNode) {
        return;
    }
    NodeGuiPtr thisNodeGui = boost::dynamic_pointer_cast<NodeGui>(thisNode->getNodeGui());
    if (!thisNodeGui) {
        return;
    }
    NodeGuiPtr currentViewerInterface = gui->getCurrentNodeViewerInterface(thisNode->getPlugin());

    if (thisNodeGui->getNode()->isEffectViewerNode()) {
        gui->removeViewerInterface(thisNodeGui, true);
    } else {
        gui->removeNodeViewerInterface(thisNodeGui, true);
    }
    gui->createNodeViewerInterface(thisNodeGui);
    if (currentViewerInterface) {
        gui->setNodeViewerInterface(currentViewerInterface);
    }

}

void
KnobGuiContainerHelperPrivate::refreshPagesEnabledness()
{
    KnobPageGuiPtr curPage = _p->getCurrentPage();

    for (PagesMap::const_iterator it = pages.begin(); it != pages.end(); ++it) {
        KnobPagePtr page = it->second->pageKnob.lock();
        if (it->second == curPage) {
            if ( !page->isEnabled(0) ) {
                page->setEnabled(0, true);
                page->evaluateValueChange(0, page->getCurrentTime(), ViewIdx(0), eValueChangedReasonUserEdited);
            }
        } else {
            if ( page->isEnabled(0) ) {
                page->setEnabled(0, false);
                page->evaluateValueChange(0, page->getCurrentTime(), ViewIdx(0), eValueChangedReasonUserEdited);
            }
        }
    }
}

void
KnobGuiContainerHelper::refreshPagesOrder(const KnobPageGuiPtr& curTabName,
                                          bool restorePageIndex)
{
    if ( !isPagingEnabled() ) {
        return;
    }
    std::list<KnobPageGuiPtr> orderedPages;
    const KnobsVec& knobs = getInternalKnobs();
    std::list<KnobPagePtr > internalPages;
    for (KnobsVec::const_iterator it = knobs.begin(); it != knobs.end(); ++it) {
        KnobPagePtr isPage = toKnobPage(*it);
        if (isPage && !isPage->getIsSecret()) {
            internalPages.push_back(isPage);
        }
    }

    for (std::list<KnobPagePtr >::iterator it = internalPages.begin(); it != internalPages.end(); ++it) {
        PagesMap::const_iterator foundPage = _imp->pages.find(*it);
        if ( foundPage != _imp->pages.end() ) {
            orderedPages.push_back(foundPage->second);
        }
    }

    setPagesOrder(orderedPages, curTabName, restorePageIndex);
}

void
KnobGuiContainerHelper::recreateKnobsInternal(const KnobPageGuiPtr& curPage,
                                              bool restorePageIndex)
{
    // Re-create knobs
    const KnobsVec& knobs = getInternalKnobs();

    initializeKnobVector(knobs);

    refreshPagesOrder(curPage, restorePageIndex);
    refreshCurrentPage();
    recreateViewerUIKnobs();


    onKnobsRecreated();
}

void
KnobGuiContainerHelper::recreateUserKnobs(bool restorePageIndex)
{
    const KnobsVec& knobs = getInternalKnobs();
    std::list<KnobPagePtr> userPages;

    getUserPages(userPages);

    KnobPageGuiPtr curPage;
    if ( isPagingEnabled() ) {
        if (restorePageIndex) {
            curPage = getCurrentPage();
        }

        for (std::list<KnobPagePtr>::iterator it = userPages.begin(); it != userPages.end(); ++it) {
            deleteKnobGui( (*it)->shared_from_this() );
        }
    } else {
        for (KnobsVec::const_iterator it = knobs.begin(); it != knobs.end(); ++it) {
            deleteKnobGui(*it);
        }
    }
    recreateKnobsInternal(curPage, restorePageIndex);
}

void
KnobGuiContainerHelper::getUserPages(std::list<KnobPagePtr>& userPages) const
{
    return _imp->holder.lock()->getUserPages(userPages);
}

void
KnobGuiContainerHelper::setPageActiveIndex(const KnobPagePtr& page)
{
    PagesMap::const_iterator foundPage = _imp->pages.find(page);

    if ( foundPage == _imp->pages.end() ) {
        return;
    }
    _imp->refreshPagesEnabledness();

    onPageActivated(foundPage->second);
}


int
KnobGuiContainerHelper::getPagesCount() const
{
    return getPages().size();
}

const QUndoCommand*
KnobGuiContainerHelper::getLastUndoCommand() const
{
    return _imp->undoStack->command(_imp->undoStack->index() - 1);
}

void
KnobGuiContainerHelper::pushUndoCommand(QUndoCommand* cmd)
{
    if ( !getGui() ) {
        delete cmd;

        return;
    }
    _imp->undoStack->setActive();
    _imp->cmdBeingPushed = cmd;
    _imp->clearedStackDuringPush = false;
    _imp->undoStack->push(cmd);

    //We may be in a situation where the command was not pushed because the stack was cleared
    if (!_imp->clearedStackDuringPush) {
        _imp->cmdBeingPushed = 0;
    }
    refreshUndoRedoButtonsEnabledNess( _imp->undoStack->canUndo(), _imp->undoStack->canRedo() );
}

boost::shared_ptr<QUndoStack>
KnobGuiContainerHelper::getUndoStack() const
{
    return _imp->undoStack;
}

void
KnobGuiContainerHelper::clearUndoRedoStack()
{
    if (_imp->undoStack) {
        _imp->undoStack->clear();
        _imp->clearedStackDuringPush = true;
        _imp->signals->s_deleteCurCmdLater();
        refreshUndoRedoButtonsEnabledNess( _imp->undoStack->canUndo(), _imp->undoStack->canRedo() );
    }
}

void
KnobGuiContainerSignalsHandler::onDeleteCurCmdLaterTriggered()
{
    _container->onDeleteCurCmdLater();
}

void
KnobGuiContainerSignalsHandler::onPageSecretnessChanged()
{
    KnobSignalSlotHandler* handler = qobject_cast<KnobSignalSlotHandler*>(sender());
    if (!handler) {
        return;
    }
    KnobPage* isPage = dynamic_cast<KnobPage*>(handler->getKnob().get());
    if (!isPage) {
        return;
    }
    const PagesMap& pages = _container->getPages();
    for (PagesMap::const_iterator it = pages.begin(); it!=pages.end(); ++it) {
        if (it->first.lock().get() == isPage) {
            if (isPage->getIsSecret()) {
                it->second->tab->setVisible(false);
                _container->removePageFromContainer(it->second);
            } else {
                _container->addPageToPagesContainer(it->second);
                it->second->tab->setVisible(true);
            }

            break;
        }
    }
}

void
KnobGuiContainerHelper::onDeleteCurCmdLater()
{
    if (_imp->cmdBeingPushed) {
        _imp->undoStack->clear();
        _imp->cmdBeingPushed = 0;
    }
}

void
KnobGuiContainerSignalsHandler::onPageLabelChangedInternally()
{
    KnobSignalSlotHandler* handler = qobject_cast<KnobSignalSlotHandler*>( sender() );

    if (!handler) {
        return;
    }
    KnobIPtr knob = handler->getKnob();
    KnobPagePtr isPage = toKnobPage(knob);
    if (!isPage) {
        return;
    }
    const PagesMap& pages = _container->getPages();
    PagesMap::const_iterator found = pages.find(isPage);
    if ( found != pages.end() ) {
        _container->onPageLabelChanged(found->second);
    }
}

void
KnobGuiContainerHelper::refreshPageVisibility(const KnobPagePtr& page)
{
    // When all knobs of a page are hidden, if the container is a tabwidget, hide the tab

    QTabWidget* isTabWidget = dynamic_cast<QTabWidget*>(getPagesContainer());
    if (!isTabWidget) {
        return;
    }



    const PagesMap& pages = getPages();

    std::list<KnobPageGuiPtr> pagesToDisplay;
    for (int i = 0; i < isTabWidget->count(); ++i) {
        QWidget* w = isTabWidget->widget(i);
        for (PagesMap::const_iterator it = pages.begin(); it!=pages.end(); ++it) {
            if (it->second->tab == w) {
                if (it->first.lock() != page) {
                    pagesToDisplay.push_back(it->second);
                } else {
                    KnobsVec children = page->getChildren();
                    bool visible = false;
                    for (KnobsVec::const_iterator it2 = children.begin(); it2 != children.end(); ++it2) {
                        visible |= !(*it2)->getIsSecret();
                    }
                    if (visible) {
                        pagesToDisplay.push_back(it->second);
                    }
                }
                break;
            }
        }
    }

    KnobPageGuiPtr curPage = getCurrentPage();
    setPagesOrder(pagesToDisplay, curPage, true);
}

NATRON_NAMESPACE_EXIT;

NATRON_NAMESPACE_USING;
#include "moc_KnobGuiContainerHelper.cpp"
