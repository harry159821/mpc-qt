#include "playlistwindow.h"
#include "ui_playlistwindow.h"
#include "qdrawnplaylist.h"
#include "playlist.h"
#include <QDragEnterEvent>
#include <QMimeData>
#include <QInputDialog>
#include <QFileDialog>
#include <QMenu>
#include <QThread>


PlaylistWindow::PlaylistWindow(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::PlaylistWindow),
    currentPlaylist(),
    showSearch(false)
{
    // When (un)docking windows, some widgets may get transformed into native
    // widgets, causing painting glitches.  Tell Qt that we prefer non-native.
    setAttribute(Qt::WA_DontCreateNativeAncestors);

    ui->setupUi(this);
    addNewTab(QUuid(), tr("Quick Playlist"));
    ui->searchHost->setVisible(false);
    ui->searchField->installEventFilter(this);

    connectSignalsToSlots();
}

PlaylistWindow::~PlaylistWindow()
{
    delete ui;
}

void PlaylistWindow::setCurrentPlaylist(QUuid what)
{
    if (tabWidgets.contains(what)) {
        ui->tabWidget->setCurrentWidget(tabWidgets[what]);
        ui->tabStack->setCurrentWidget(listWidgets[what]);
        currentPlaylist = what;
    }
    updateCurrentPlaylist();
}

void PlaylistWindow::clearPlaylist(QUuid what)
{
    if (listWidgets.contains(what))
        listWidgets[what]->removeAll();
}

QPair<QUuid, QUuid> PlaylistWindow::addToCurrentPlaylist(QList<QUrl> what)
{
    QPair<QUuid, QUuid> info;
    auto qdp = currentPlaylistWidget();
    for (QUrl url : what) {
        QPair<QUuid,QUuid> itemInfo = qdp->importUrl(url);
        if (info.second.isNull())
            info = itemInfo;
    }
    return info;
}

QPair<QUuid, QUuid> PlaylistWindow::urlToQuickPlaylist(QUrl what)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(QUuid());
    pl->clear();
    listWidgets[QUuid()]->clear();
    ui->tabWidget->setCurrentWidget(tabWidgets[QUuid()]);
    ui->tabStack->setCurrentWidget(listWidgets[QUuid()]);
    return addToCurrentPlaylist(QList<QUrl>() << what);
}

bool PlaylistWindow::isCurrentPlaylistEmpty()
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(currentPlaylist);
    return pl ? pl->isEmpty() : true;
}

QUuid PlaylistWindow::getItemAfter(QUuid list, QUuid item)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return QUuid();
    QUuid uuid = pl->queueTakeFirst();
    if (!uuid.isNull())
        return uuid;
    QSharedPointer<Item> after = pl->itemAfter(item);
    if (!after)
        return QUuid();
    return after->uuid();
}

QUuid PlaylistWindow::getItemBefore(QUuid list, QUuid item)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return QUuid();
    QSharedPointer<Item> before = pl->itemBefore(item);
    if (!before)
        return QUuid();
    return before->uuid();
}

QUrl PlaylistWindow::getUrlOf(QUuid list, QUuid item)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return QUrl();
    auto i = pl->itemOf(item);
    if (!i)
        return QUrl();
    return i->url();
}

void PlaylistWindow::setMetadata(QUuid list, QUuid item, const QVariantMap &map)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list);
    if (!pl)
        return;
    auto i = pl->itemOf(item);
    if (!i)
        return;
    i->setMetadata(map);

    auto qdp = currentPlaylistWidget();
    if (qdp->uuid() == list)
        qdp->viewport()->update();

}

QVariantList PlaylistWindow::tabsToVList() const
{
    QVariantList qvl;
    for (int i = 0; i < ui->tabWidget->count(); i++) {
        auto tab = reinterpret_cast<EmptyTab *>(ui->tabWidget->widget(i));
        auto widget = tab->partner();
        qvl.append(widget->toVMap());
    }
    return qvl;
}

void PlaylistWindow::tabsFromVList(const QVariantList &qvl)
{
    ui->tabWidget->clear();
    while (ui->tabStack->count()) ui->tabStack->removeWidget(ui->tabStack->widget(0));
    listWidgets.clear();
    tabWidgets.clear();
    for (const QVariant &v : qvl) {
        auto qdp = new QDrawnPlaylist();
        qdp->setDisplayParser(&displayParser);
        qdp->fromVMap(v.toMap());
        connect(qdp, &QDrawnPlaylist::itemDesired, this, &PlaylistWindow::itemDesired);
        auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
        listWidgets.insert(pl->uuid(), qdp);

        EmptyTab *tab = new EmptyTab(this);
        tabWidgets.insert(pl->uuid(), tab);
        tab->setPartner(qdp);

        ui->tabStack->addWidget(qdp);
        ui->tabWidget->addTab(tab, pl->title());

    }
    if (tabWidgets.count() < 1)
        addNewTab(QUuid(), tr("Quick Playlist"));
}

bool PlaylistWindow::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj);
    if (obj == ui->searchField && event->type() == QEvent::KeyPress) {
        auto keyEvent = reinterpret_cast<QKeyEvent*>(event);
        if (!keyEvent->modifiers() &&
                (keyEvent->key() == Qt::Key_Up ||
                 keyEvent->key() == Qt::Key_Down)) {
            if (keyEvent->key() == Qt::Key_Up)
                selectPrevious();
            else
                selectNext();
            return true;
        }
    }
    return QDockWidget::eventFilter(obj, event);
}

void PlaylistWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->accept();
}

void PlaylistWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    addToCurrentPlaylist(event->mimeData()->urls());
}

void PlaylistWindow::wheelEvent(QWheelEvent *event)
{
    // Don't pass scroll events up the chain.  They are used for e.g. tab
    // switching when over the tab bar and also scrolling the playlists.
    event->accept();
}

void PlaylistWindow::connectSignalsToSlots()
{
    connect(this, &PlaylistWindow::visibilityChanged,
            this, &PlaylistWindow::self_visibilityChanged);

    connect(ui->newTab, &QPushButton::clicked,
            this, &PlaylistWindow::newTab);
    connect(ui->closeTab, &QPushButton::clicked,
            this, &PlaylistWindow::closeTab);
    connect(ui->duplicateTab, &QPushButton::clicked,
            this, &PlaylistWindow::duplicateTab);
    connect(ui->importList, &QPushButton::clicked,
            this, &PlaylistWindow::importTab);
    connect(ui->exportList, &QPushButton::clicked,
            this, &PlaylistWindow::exportTab);
    connect(ui->visibleToQueue, &QPushButton::clicked,
            this, &PlaylistWindow::visibleToQueue);
}

QDrawnPlaylist *PlaylistWindow::currentPlaylistWidget()
{
    auto tab = reinterpret_cast<EmptyTab *>(ui->tabWidget->currentWidget());
    return tab ? tab->partner() : NULL;
}

void PlaylistWindow::updateCurrentPlaylist()
{
    auto qdp = currentPlaylistWidget();
    if (!qdp)
        return;
    currentPlaylist = qdp->uuid();
    ui->tabStack->setCurrentWidget(listWidgets[currentPlaylist]);
    ui->tabStack->setFocusProxy(listWidgets[currentPlaylist]);
    setTabOrder(ui->tabWidget, ui->tabStack->focusProxy());
    setTabOrder(ui->tabStack->focusProxy(), ui->searchField);
}

void PlaylistWindow::setPlaylistFilters(QString filterText)
{
    for (auto widget : listWidgets) {
        widget->setFilter(filterText);
    }
}

void PlaylistWindow::addNewTab(QUuid playlist, QString title)
{
    auto qdp = new QDrawnPlaylist();
    listWidgets.insert(playlist, qdp);
    EmptyTab *tab = new EmptyTab(this);
    tabWidgets.insert(playlist, tab);
    tab->setPartner(qdp);

    qdp->setDisplayParser(&displayParser);
    qdp->setUuid(playlist);
    connect(qdp, &QDrawnPlaylist::itemDesired, this, &PlaylistWindow::itemDesired);

    ui->tabStack->addWidget(qdp);
    ui->tabWidget->addTab(tab, title);
    ui->tabWidget->setCurrentWidget(tab);
}

void PlaylistWindow::changePlaylistSelection( QUrl itemUrl, QUuid playlistUuid, QUuid itemUuid)
{
    (void)itemUrl;
    if (!tabWidgets.contains(playlistUuid))
        return;
    auto qdp = listWidgets[playlistUuid];
    auto pl = PlaylistCollection::getSingleton()->playlistOf(playlistUuid);
    if (!itemUuid.isNull() && pl->queueFirst() == itemUuid)
        pl->queueTakeFirst();
    qdp->scrollToItem(itemUuid);
    qdp->setNowPlayingItem(itemUuid);
}

void PlaylistWindow::addSimplePlaylist(QStringList data)
{
    auto pl = PlaylistCollection::getSingleton()->newPlaylist(tr("New Playlist"));
    pl->fromStringList(data);
    addNewTab(pl->uuid(), pl->title());
}

void PlaylistWindow::setDisplayFormatSpecifier(QString fmt)
{
    displayParser.takeFormatString(fmt);
    ui->tabStack->currentWidget()->update();
}

void PlaylistWindow::newTab()
{
    auto pl = PlaylistCollection::getSingleton()->newPlaylist(tr("New Playlist"));
    addNewTab(pl->uuid(), pl->title());
}

void PlaylistWindow::closeTab()
{
    int index = ui->tabWidget->currentIndex();
    on_tabWidget_tabCloseRequested(index);
    updateCurrentPlaylist();
}

void PlaylistWindow::duplicateTab()
{
    auto origin = currentPlaylistWidget();
    auto remote = PlaylistCollection::getSingleton()->clonePlaylist(origin->uuid());
    addNewTab(remote->uuid(), remote->title());
}

void PlaylistWindow::importTab()
{
    QString file;
    file = QFileDialog::getOpenFileName(this, tr("Import File"), QString(),
                                        tr("Playlist files (*.m3u *.m3u8)"));
    if (!file.isEmpty())
        emit importPlaylist(file);
}

void PlaylistWindow::exportTab()
{
    auto uuid = currentPlaylistWidget()->uuid();

    QString file;
    file = QFileDialog::getSaveFileName(this, tr("Export File"), QString(),
                                        tr("Playlist files (*.m3u *.m3u8)"));
    auto pl = PlaylistCollection::getSingleton()->playlistOf(uuid);
    if (!file.isEmpty() && pl)
        emit exportPlaylist(file, pl->toStringList());
}

void PlaylistWindow::playCurrentItem()
{
    auto qdp = currentPlaylistWidget();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
    auto itemUuid = qdp->currentItemUuid();
    if (itemUuid.isNull())
        return;
    emit itemDesired(pl->uuid(), itemUuid);
}

void PlaylistWindow::selectNext()
{
    auto qdp = currentPlaylistWidget();
    int index = qdp->currentRow();
    if (index < qdp->count())
        qdp->setCurrentRow(index + 1);
}

void PlaylistWindow::selectPrevious()
{
    auto qdp = currentPlaylistWidget();
    int index = qdp->currentRow();
    if (index > 0)
        qdp->setCurrentRow(index - 1);
}

void PlaylistWindow::quickQueue()
{
    auto qdp = currentPlaylistWidget();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(qdp->uuid());
    auto itemUuid = qdp->currentItemUuid();
    if (itemUuid.isNull())
        return;
    pl->queueToggle(itemUuid);
    qdp->viewport()->update();
}

void PlaylistWindow::visibleToQueue()
{
    currentPlaylistWidget()->visibleToQueue();
}

void PlaylistWindow::revealSearch()
{
    showSearch = true;
    activateWindow();
    ui->searchHost->setVisible(true);
    ui->searchField->setFocus();
}

void PlaylistWindow::finishSearch()
{
    showSearch = false;
    if (!ui->searchHost->isVisible())
        return;

    if (!ui->searchField->text().isEmpty()) {
        ui->searchField->setText(QString());
        setPlaylistFilters(QString());
    }

    ui->searchHost->setVisible(false);
}

void PlaylistWindow::self_relativeSeekRequested(bool forwards, bool small)
{
    emit relativeSeekRequested(forwards, small);
}

void PlaylistWindow::self_visibilityChanged()
{
    // When the window was (re)created/destroyed for whatever reason by
    // the toolkit/wm/etc, reveal the search widget if it was active last.
    if (showSearch)
        revealSearch();
    else
        finishSearch();
}

void PlaylistWindow::on_tabWidget_tabCloseRequested(int index)
{
    int current = ui->tabWidget->currentIndex();
    auto tab = reinterpret_cast<EmptyTab *>(ui->tabWidget->widget(index));
    auto qdp = tab->partner();
    if (!qdp)
        return;
    if (qdp->uuid().isNull()) {
        qdp->removeAll();
    } else {
        PlaylistCollection::getSingleton()->removePlaylist(qdp->uuid());
        tabWidgets.remove(qdp->uuid());
        listWidgets.remove(qdp->uuid());
        ui->tabWidget->removeTab(index);
        ui->tabStack->removeWidget(qdp);
    }
    if (current == index)
        updateCurrentPlaylist();
}

void PlaylistWindow::on_tabWidget_tabBarDoubleClicked(int index)
{
    auto tab = reinterpret_cast<EmptyTab *>(ui->tabWidget->widget(index));
    auto widget = tab->partner();
    QUuid tabUuid = widget->uuid();
    if (tabUuid.isNull())
        return;
    QInputDialog *qid = new QInputDialog(this);
    qid->setAttribute(Qt::WA_DeleteOnClose);
    qid->setWindowModality(Qt::ApplicationModal);
    qid->setWindowTitle(tr("Enter playlist name"));
    qid->setTextValue(ui->tabWidget->tabText(index));
    connect(qid, &QInputDialog::accepted, [=]() {
        int tabIndex = ui->tabWidget->indexOf(widget);
        if (tabIndex < 0)
            return;
        auto pl = PlaylistCollection::getSingleton()->playlistOf(tabUuid);
        if (!pl)
            return;
        pl->setTitle(qid->textValue());
        ui->tabWidget->setTabText(tabIndex, qid->textValue());
    });
    qid->show();
}

void PlaylistWindow::on_tabWidget_customContextMenuRequested(const QPoint &pos)
{
    QMenu *m = new QMenu(this);
    m->addAction(tr("&New Playlist"), this, SLOT(newTab()));
    m->addAction(tr("&Remove Playlist"), this, SLOT(closeTab()));
    m->addAction(tr("&Duplicate Playlist"), this, SLOT(duplicateTab()));
    m->addAction(tr("&Import Playlist"), this, SLOT(importTab()));
    m->addAction(tr("&Export Playlist"), this, SLOT(exportTab()));
    m->exec(ui->tabWidget->mapToGlobal(pos));
}

void PlaylistWindow::on_searchField_textEdited(const QString &arg1)
{
    setPlaylistFilters(arg1);
}

void PlaylistWindow::on_tabWidget_currentChanged(int index)
{
    updateCurrentPlaylist();
}

void PlaylistWindow::on_searchField_returnPressed()
{
    playCurrentItem();
}
