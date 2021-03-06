#include <QApplication>
#include <QThread>
#include <QPainter>
#include <QFontMetrics>
#include <QMenu>
#include <QKeyEvent>
#include "qdrawnplaylist.h"
#include "playlist.h"
#include "helpers.h"

PlayPainter::PlayPainter(QObject *parent) : QAbstractItemDelegate(parent) {}

void PlayPainter::paint(QPainter *painter, const QStyleOptionViewItem &option,
                        const QModelIndex &index) const
{
    auto playWidget = qobject_cast<QDrawnPlaylist*>(parent());
    auto p = playWidget->playlist();
    if (p == NULL)
        return;
    QSharedPointer<Item> i = p->itemOf(QUuid(index.data(Qt::DisplayRole).toString()));
    if (i == NULL)
        return;

    QStyleOptionViewItem o2 = option;
    if (playWidget->currentItemUuid() == i->uuid()) {
        // in some cases, the current item we want to highlight is not the
        // actual item that the list widget thinks is selected. i.e. during
        // searching the topmost item should be selected.
        o2.showDecorationSelected = true;
        o2.state |= QStyle::State_Selected;
    }
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &o2,
                                       painter);
    QRect rc = option.rect.adjusted(3,0,-3,0);

    if (i->queuePosition() || i->extraPlayTimes()) {
        QString extraText;
        if (i->queuePosition())
            extraText.append(QString::number(i->queuePosition()));
        if (i->extraPlayTimes())
            extraText.append(QString("+%1").arg(i->extraPlayTimes()));
        int extraTextWidth = painter->fontMetrics().width(extraText);
        QRect rc2(rc);
        rc2.setLeft(rc.right() - extraTextWidth);
        painter->drawText(rc2, Qt::AlignRight|Qt::AlignVCenter, extraText);
        rc.adjust(0, 0, -(3 + extraTextWidth), 0);
    }

    DisplayParser *dp = playWidget->displayParser();
    QString text = i->toDisplayString();
    if (dp) {
        // TODO: detect what type of file is being played
        text = dp->parseMetadata(i->metadata(), text, Helpers::VideoFile);
    }

    QFont f = playWidget->font();
    f.setBold(i->uuid() == playWidget->nowPlayingItem());
    painter->setFont(f);
    painter->setPen(playWidget->palette().text().color());
    painter->drawText(rc, Qt::AlignLeft|Qt::AlignVCenter,
                      text);
    painter->setFont(playWidget->font());
}

QSize PlayPainter::sizeHint(const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    Q_UNUSED(index);
    return QApplication::style()->sizeFromContents(QStyle::CT_ItemViewItem,
                                                   &option,
                                                   option.rect.size());
}

PlayItem::PlayItem(const QUuid &uuid, const QUuid &playlistUuid,
                   QListWidget *parent)
    : QListWidgetItem(parent, QListWidgetItem::UserType)
{
    playlistUuid_ = playlistUuid;
    uuid_ = uuid;
    setData(Qt::DisplayRole, uuid.toString());
}

PlayItem::~PlayItem()
{

}

QUuid PlayItem::playlistUuid()
{
    return playlistUuid_;
}

QUuid PlayItem::uuid()
{
    return uuid_;
}

QDrawnPlaylist::QDrawnPlaylist(QWidget *parent) : QListWidget(parent),
    displayParser_(NULL), worker(NULL), searcher(NULL)
{
    worker = new QThread();
    worker->start();
    searcher = new PlaylistSearcher();
    searcher->moveToThread(worker);

    setSelectionMode(QAbstractItemView::ContiguousSelection);
    setDragDropMode(QAbstractItemView::InternalMove);

    setItemDelegate(new PlayPainter(this));

    connect(worker, &QThread::finished, searcher, &QObject::deleteLater);
    connect(model(), SIGNAL(rowsAboutToBeMoved(QModelIndex,int,int,QModelIndex,int)),
            this, SLOT(model_rowsMoved(QModelIndex,int,int,QModelIndex,int)));
    connect(this, &QDrawnPlaylist::searcher_filterPlaylist,
            searcher, &PlaylistSearcher::filterPlaylist,
            Qt::QueuedConnection);
    connect(searcher, &PlaylistSearcher::playlistFiltered,
            this, &QDrawnPlaylist::repopulateItems,
            Qt::QueuedConnection);
    connect(this, &QDrawnPlaylist::currentItemChanged,
            this, &QDrawnPlaylist::self_currentItemChanged);
    connect(this, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this, SLOT(self_itemDoubleClicked(QListWidgetItem*)));
    connect(this, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(self_customContextMenuRequested(QPoint)));
    setContextMenuPolicy(Qt::CustomContextMenu);
}

QDrawnPlaylist::~QDrawnPlaylist()
{
    worker->deleteLater();
}

QSharedPointer<Playlist> QDrawnPlaylist::playlist() const
{
    return PlaylistCollection::getSingleton()->playlistOf(uuid_);
}

QUuid QDrawnPlaylist::uuid() const
{
    return uuid_;
}

QUuid QDrawnPlaylist::currentItemUuid() const
{
    PlayItem *item = reinterpret_cast<PlayItem*>(currentItem());
    if (!item)
        item = reinterpret_cast<PlayItem*>(QListWidget::item(0));
    if (item)
        return item->uuid();
    return QUuid();
}

QList<QUuid> QDrawnPlaylist::currentItemUuids() const
{
    QList<QUuid> selected;
    for (auto i : selectedItems())
        selected.append(QUuid(i->text()));
    return selected;
}

void QDrawnPlaylist::traverseSelected(std::function<void (QUuid)> callback)
{
    for (auto i : selectedItems())
        callback(QUuid(i->text()));
}

void QDrawnPlaylist::setCurrentItem(QUuid itemUuid)
{
    QList<QListWidgetItem *> items = findItems(itemUuid.toString(), Qt::MatchExactly);
    if (items.isEmpty())
        setCurrentRow(-1);
    else
        QListWidget::setCurrentItem(items.first());
}

void QDrawnPlaylist::scrollToItem(QUuid itemUuid)
{
    auto items = findItems(itemUuid.toString(), Qt::MatchExactly);
    if (items.empty())
        return;
    auto item = items.first();
    scrollTo(indexFromItem(item));
}

void QDrawnPlaylist::setUuid(const QUuid &uuid)
{
    uuid_ = uuid;
    repopulateItems();
}

void QDrawnPlaylist::addItem(QUuid uuid)
{
    PlayItem *playItem = new PlayItem(uuid, uuid_, this);
    QListWidget::addItem(playItem);
}

void QDrawnPlaylist::addItems(const QList<QUuid> &items)
{
    for (const QUuid &item : items)
        QListWidget::addItem(item.toString());
}

void QDrawnPlaylist::addItemsAfter(QUuid item, const QList<QUuid> &items)
{
    auto matchingRows = findItems(item.toString(), Qt::MatchExactly);
    if (matchingRows.length() < 1)
        return;
    int itemIndex = row(matchingRows[0]) + 1;
    for (auto i : items) {
        PlayItem *playItem = new PlayItem(i);
        QListWidget::insertItem(itemIndex++, playItem);
    }
}

void QDrawnPlaylist::removeItem(QUuid uuid)
{
    QSharedPointer<Playlist> playlist = this->playlist();
    if (playlist && playlist->contains(uuid))
        playlist->removeItem(uuid);
    auto matchingRows = findItems(uuid.toString(), Qt::MatchExactly);
    if (matchingRows.length() > 0)
        takeItem(row(matchingRows[0]));
}

void QDrawnPlaylist::removeItems(const QList<int> &indicies)
{
    QListIterator<int> iterator(indicies);
    iterator.toBack();
    while (iterator.hasPrevious())
        delete takeItem(iterator.previous());
}

void QDrawnPlaylist::removeAll()
{
    QSharedPointer<Playlist> p = playlist();
    if (!p)
        return;
    p->clear();
    clear();
}

QPair<QUuid,QUuid> QDrawnPlaylist::importUrl(QUrl url)
{
    QPair<QUuid,QUuid> info;
    QSharedPointer<Playlist> playlist = this->playlist();
    if (!playlist)  return info;
    auto item = playlist->addItem(url);
    info.first = uuid_;
    info.second = item->uuid();
    if (currentFilterText.isEmpty() ||
            PlaylistSearcher::itemMatchesFilter(item, currentFilterList))
        addItem(item->uuid());
    return info;
}

void QDrawnPlaylist::currentToQueue()
{
    // CHECKME: code for this should be here?
}

QUuid QDrawnPlaylist::nowPlayingItem()
{
    QSharedPointer<Playlist> playlist = this->playlist();
    if (nowPlayingItem_.isNull() || !playlist->contains(nowPlayingItem_))
        nowPlayingItem_ = currentItemUuid();
    return nowPlayingItem_;
}

void QDrawnPlaylist::setNowPlayingItem(QUuid uuid)
{
    QSharedPointer<Playlist> playlist = this->playlist();
    bool refreshNeeded = playlist->contains(nowPlayingItem()) || playlist->contains(uuid);
    nowPlayingItem_ = uuid;
    if (refreshNeeded)
        viewport()->repaint();
}

QVariantMap QDrawnPlaylist::toVMap() const
{
    QSharedPointer<Playlist> playlist = this->playlist();
    if (!playlist)
        return QVariantMap();
    QVariantMap qvm;
    qvm.insert("nowplaying", nowPlayingItem_);
    qvm.insert("contents", playlist->toVMap());
    return qvm;
}

void QDrawnPlaylist::fromVMap(const QVariantMap &qvm)
{
    QVariantMap contents = qvm.value("contents").toMap();
    QSharedPointer<Playlist> p(new Playlist);
    p->fromVMap(contents);
    PlaylistCollection::getSingleton()->addPlaylist(p);
    setUuid(p->uuid());
    nowPlayingItem_ = qvm.value("nowplaying").toUuid();
    setCurrentItem(nowPlayingItem_);
}

void QDrawnPlaylist::setDisplayParser(DisplayParser *parser)
{
    displayParser_ = parser;
}

DisplayParser *QDrawnPlaylist::displayParser()
{
    return displayParser_;
}

void QDrawnPlaylist::setFilter(QString needles)
{
    if (currentFilterText == needles)
        return;

    currentFilterText = needles;
    currentFilterList = PlaylistSearcher::textToNeedles(needles);
    searcher->bump();
    emit searcher_filterPlaylist(playlist(), needles);
}

bool QDrawnPlaylist::event(QEvent *e)
{
    if (!hasFocus())
        goto end;
    if (e->type() == QEvent::ShortcutOverride) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_PageUp || ke->key() == Qt::Key_PageDown) {
            e->accept();
            return true;
        }
    }
    end:
    return QListWidget::event(e);
}

void QDrawnPlaylist::repopulateItems()
{
    clear();

    auto playlist = this->playlist();
    if (playlist == NULL)
        return;

    auto itemAdder = [&](QSharedPointer<Item> item) {
        if (!item->hidden())
            addItem(item->uuid());
    };
    playlist->iterateItems(itemAdder);
    setCurrentItem(lastSelectedItem);
}

void QDrawnPlaylist::model_rowsMoved(const QModelIndex &parent,
                                     int start, int end,
                                     const QModelIndex &destination, int row)
{
    Q_UNUSED(parent);
    Q_UNUSED(destination);
    QSharedPointer<Playlist> p = playlist();
    if (p.isNull())
        return;
    QListWidgetItem *destinationItem = QListWidget::item(row);
    QUuid destinationId = destinationItem ? QUuid(destinationItem->text())
                                          : QUuid();
    QList<QSharedPointer<Item>> itemsToGrab;
    for (int index = start; index <= end; index++) {
        QUuid itemUuid = QUuid(QListWidget::item(index)->text());
        itemsToGrab.append(p->itemOf(itemUuid));
    }
    p->takeItemsRaw(itemsToGrab);
    p->addItems(destinationId, itemsToGrab);
}

void QDrawnPlaylist::self_currentItemChanged(QListWidgetItem *current,
                                             QListWidgetItem *previous)
{
    Q_UNUSED(previous);
    QUuid uuid;
    if (current && !(uuid=reinterpret_cast<PlayItem*>(current)->uuid()).isNull())
        lastSelectedItem = uuid;
}

void QDrawnPlaylist::self_itemDoubleClicked(QListWidgetItem *item)
{
    PlayItem *playItem = reinterpret_cast<PlayItem*>(item);
    itemDesired(playItem->playlistUuid(), playItem->uuid());
}

void QDrawnPlaylist::self_customContextMenuRequested(const QPoint &p)
{
    QMenu *m = new QMenu(this);
    QAction *a = new QAction(m);
    a->setText(tr("Remove"));
    connect(a, &QAction::triggered, [=]() {
        traverseSelected([this](const QUuid &item) { removeItem(item); });
    });
    m->addAction(a);
    a = new QAction(m);
    a->setText(tr("Remove All"));
    connect(a, &QAction::triggered, this, &QDrawnPlaylist::removeAll);
    m->addAction(a);
    connect(m, &QMenu::aboutToHide, [=]() {
        m->deleteLater();
    });
    m->exec(mapToGlobal(p));
}



class PlaylistSelectionPrivate {
public:
    QList<QSharedPointer<Item>> items;
};


PlaylistSelection::PlaylistSelection()
{
    d = new PlaylistSelectionPrivate();
}

PlaylistSelection::PlaylistSelection(PlaylistSelection &other)
{
    d = new PlaylistSelectionPrivate();
    other.d->items = d->items;
}

PlaylistSelection::~PlaylistSelection()
{
    delete d;
}

void PlaylistSelection::fromItem(QUuid playlistUuid, QUuid itemUuid)
{
    d->items.clear();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(playlistUuid);
    if (Q_UNLIKELY(!pl))
        return;
    auto i = pl->itemOf(itemUuid);
    if (Q_LIKELY(!i.isNull()))
        d->items.append(i);
}

void PlaylistSelection::fromQueue(QDrawnPlaylist *list)
{
    d->items.clear();
    auto queue = PlaylistCollection::getSingleton()->queuePlaylist();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list->uuid());
    if (Q_UNLIKELY(!pl))
        return;
    auto itemAdder = [this](QSharedPointer<Item> item) {
        d->items.append(item);
    };
    queue->iterateItems(itemAdder);
    if (d->items.isEmpty()) {
        //CHECKME: will this still correct when the queue widget is shown?
        QUuid activeItem = list->nowPlayingItem();
        if (!activeItem.isNull())
            d->items.append(pl->itemOf(activeItem));
    }
}

void PlaylistSelection::fromSelected(QDrawnPlaylist *list)
{
    d->items.clear();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list->uuid());
    if (Q_UNLIKELY(!pl))
        return;
    auto itemAdder = [this,pl](QUuid item) {
        d->items.append(pl->itemOf(item));
    };
    list->traverseSelected(itemAdder);
}

void PlaylistSelection::appendToPlaylist(QDrawnPlaylist *list)
{
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list->uuid());
    if (Q_UNLIKELY(!pl))
        return;
    for (auto i : d->items) {
        list->addItem(pl->addItemClone(i)->uuid());
    }
    list->viewport()->update();
}

void PlaylistSelection::appendAndQuickQueue(QDrawnPlaylist *list)
{
    auto queue = PlaylistCollection::getSingleton()->queuePlaylist();
    auto pl = PlaylistCollection::getSingleton()->playlistOf(list->uuid());
    if (Q_UNLIKELY(!pl))
        return;
    for (auto i : d->items) {
        QUuid uuid = pl->addItemClone(i)->uuid();
        list->addItem(uuid);
        queue->toggle(pl->uuid(), uuid);
    }
    list->viewport()->update();
}

QSharedPointer<Playlist> QDrawnQueue::playlist() const
{
    return PlaylistCollection::getSingleton()->queuePlaylist();
}

void QDrawnQueue::addItem(QUuid uuid)
{
    auto actualItem = ItemCollection::getSingleton()->itemOf(uuid);
    if (actualItem.isNull())
        return;
    PlayItem *playItem = new PlayItem(uuid, actualItem->playlistUuid(), this);
    QListWidget::addItem(playItem);
}
