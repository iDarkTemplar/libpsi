/*
 * Copyright (C) 2009  Barracuda Networks, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "groupchatbrowsewindow.h"

#include <QStandardItem>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenu>
#include "ui_groupchatbrowsewindow.h"

// TODO: recent list of joins, like original gcjoindlg?

Q_DECLARE_METATYPE(XMPP::Jid)
Q_DECLARE_METATYPE(GroupChatBrowseWindow::RoomOptions)
Q_DECLARE_METATYPE(GroupChatBrowseWindow::RoomInfo)

enum Role
{
	RoomInfoRole = Qt::UserRole
};

// TODO: handle duplicate rooms somehow
class RoomModel : public QStandardItemModel
{
	Q_OBJECT

public:
	QList<GroupChatBrowseWindow::RoomInfo> list;
	//QPixmap icon;

	RoomModel(QObject *parent = 0) :
		QStandardItemModel(parent)
	{
		//icon = QPixmap("groupchat.png");
		setColumnCount(1);

		QStringList headers;
		headers += tr("Groupchat name");
		//headers += tr("Participants");
		setHorizontalHeaderLabels(headers);
		//setSortRole(RoomItem::PositionRole);
	}

	void addRooms(const QList<GroupChatBrowseWindow::RoomInfo> &alist)
	{
		list += alist;
		foreach(const GroupChatBrowseWindow::RoomInfo &info, alist)
		{
			QList<QStandardItem*> clist;
			clist += new QStandardItem(/*icon*/ QPixmap(), info.roomName);
			clist[0]->setData(qVariantFromValue(info), RoomInfoRole);
			//clist += new QStandardItem(QString::number(info.participants));
			appendRow(clist);
		}
	}

	void removeRoom(int at)
	{
		list.removeAt(at);
		removeRow(at);
	}
};

class PsiGroupChatBrowseWindow::Private : public QObject
{
	Q_OBJECT

public:
	PsiGroupChatBrowseWindow *q;
	Ui::GroupChatBrowseWindowUI ui;
	XMPP::Jid server;
	RoomModel *model;
	QObject *controller; // FIXME: remove this

	XMPP::Jid roomBeingCreated, roomBeingDestroyed;
	QPushButton *pb_create, *pb_join;

	Private(PsiGroupChatBrowseWindow *_q) :
		QObject(_q),
		q(_q),
		controller(0)
	{
		ui.setupUi(q);

		model = new RoomModel(this);

		ui.tv_rooms->setAllColumnsShowFocus(true);
		ui.tv_rooms->setRootIsDecorated(false);
		ui.tv_rooms->setSortingEnabled(true);
		ui.tv_rooms->sortByColumn(0, Qt::AscendingOrder);
		ui.tv_rooms->header()->setMovable(false);
		ui.tv_rooms->header()->setClickable(true);
		ui.tv_rooms->setModel(model);
		ui.buttonBox->setStandardButtons(QDialogButtonBox::Close);
		pb_create = new QPushButton("Cre&ate...", q);
		pb_join = new QPushButton("&Join", q);
		pb_join->setDefault(true);
		ui.buttonBox->addButton(pb_create, QDialogButtonBox::ActionRole);
		ui.buttonBox->addButton(pb_join, QDialogButtonBox::AcceptRole);

		ui.tv_rooms->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(ui.tv_rooms, SIGNAL(customContextMenuRequested(const QPoint &)), SLOT(rooms_contextMenuRequested(const QPoint &)));

		connect(ui.tv_rooms->selectionModel(), SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)), SLOT(rooms_selectionChanged(const QItemSelection &, const QItemSelection &)));
		connect(ui.le_room, SIGNAL(textChanged(const QString &)), SLOT(room_textChanged(const QString &)));

		connect(pb_create, SIGNAL(clicked()), SLOT(doCreate()));
		connect(pb_join, SIGNAL(clicked()), SLOT(doJoin()));
		connect(ui.buttonBox, SIGNAL(rejected()), SLOT(doClose()));
		pb_join->setEnabled(false);

		q->resize(560, 420);
	}

	void setWidgetsEnabled(bool enabled)
	{
		ui.tv_rooms->setEnabled(enabled);
		ui.le_room->setEnabled(enabled);
		ui.le_nick->setEnabled(enabled);
		pb_create->setEnabled(enabled);
		pb_join->setEnabled(enabled);
	}

	void roomDestroyed()
	{
		int at = -1;
		for(int n = 0; n < model->list.count(); ++n)
		{
			if(model->list[n].jid == roomBeingDestroyed)
			{
				at = n;
				break;
			}
		}
		if(at == -1)
			return;

		model->removeRoom(at);
	}

public slots:
	void rooms_selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
	{
		Q_UNUSED(deselected);

		if(!selected.indexes().isEmpty() || !ui.le_room->text().isEmpty())
			pb_join->setEnabled(true);
		else
			pb_join->setEnabled(false);
	}

	void rooms_contextMenuRequested(const QPoint &pos)
	{
		QItemSelection selected = ui.tv_rooms->selectionModel()->selection();

		if(!selected.indexes().isEmpty())
		{
			QPoint gpos = ui.tv_rooms->viewport()->mapToGlobal(pos);
			QMenu menu(q);
			QAction *destroyAction = menu.addAction(tr("Destroy"));
			QAction *act = menu.exec(gpos);
			if(act == destroyAction)
			{
				QModelIndex index = selected.indexes().first();
				// ### async is bad
				QMetaObject::invokeMethod(this, "destroyRoom", Qt::QueuedConnection,
					Q_ARG(XMPP::Jid, model->list[index.row()].jid));
			}
		}
	}

	void room_textChanged(const QString &text)
	{
		Q_UNUSED(text);

		QItemSelection selected = ui.tv_rooms->selectionModel()->selection();

		if(!selected.indexes().isEmpty() || !ui.le_room->text().isEmpty())
			pb_join->setEnabled(true);
		else
			pb_join->setEnabled(false);
	}

	void doCreate()
	{
		QString room = QInputDialog::getText(q, tr("Create Groupchat"),
			tr("Choose a name for the groupchat you want to create"));

		if(!room.isEmpty())
		{
			setWidgetsEnabled(false);

			roomBeingCreated = server.withNode(room);
			emit q->onCreate(roomBeingCreated);
		}
	}

	void doJoin()
	{
		XMPP::Jid room;

		QString manualRoom = ui.le_room->text();
		if(!manualRoom.isEmpty())
		{
			if(manualRoom.indexOf('@') != -1)
				room = manualRoom;
			else
				room = server.withNode(manualRoom);
		}
		else
		{
			QItemSelection selection = ui.tv_rooms->selectionModel()->selection();
			if(selection.indexes().isEmpty())
				return;
			QModelIndex index = selection.indexes().first();

			room = model->list[index.row()].jid;
		}

		if(!room.isEmpty())
		{
			setWidgetsEnabled(false);

			emit q->onJoin(room);
		}
	}

	void doClose()
	{
		q->close();
	}

	void createFinalize()
	{
		emit q->onCreateFinalize(roomBeingCreated, true);
		q->close();
	}

	void destroyRoom(const XMPP::Jid &room)
	{
		setWidgetsEnabled(false);
		roomBeingDestroyed = room;
		emit q->onDestroy(room);
	}
};

PsiGroupChatBrowseWindow::PsiGroupChatBrowseWindow(QWidget *parent) :
	GroupChatBrowseWindow(parent)
{
	qRegisterMetaType<XMPP::Jid>();
	qRegisterMetaType<GroupChatBrowseWindow::RoomOptions>();

	d = new Private(this);
}

PsiGroupChatBrowseWindow::~PsiGroupChatBrowseWindow()
{
	delete d;
}

void PsiGroupChatBrowseWindow::resizeEvent(QResizeEvent *event)
{
	/*
	//int sort_margin = d->ui.tv_rooms->header()->style()->pixelMetric(QStyle::PM_HeaderMargin);
	int grip_width = d->ui.tv_rooms->header()->style()->pixelMetric(QStyle::PM_HeaderGripMargin);
	int frame_width = d->ui.tv_rooms->frameWidth();
	//printf("s=%d,g=%d,f=%d,h=%d\n", sort_margin, grip_width, frame_width, d->ui.tv_rooms->header()->frameWidth());
	grip_width *= 2; // HACK: this is certainly wrong, but some styles need extra pixel shifting
	frame_width *= 2; // frame on left and right side
	int widget_width = d->ui.tv_rooms->width();
	int right_column_ideal = d->ui.tv_rooms->header()->sectionSizeHint(1);
	int left_column_width = widget_width - right_column_ideal - grip_width - frame_width;
	d->ui.tv_rooms->header()->resizeSection(0, left_column_width);*/
	GroupChatBrowseWindow::resizeEvent(event);
}

QObject *PsiGroupChatBrowseWindow::controller() const
{
	return d->controller;
}

void PsiGroupChatBrowseWindow::setController(QObject *controller)
{
	d->controller = controller;
}

void PsiGroupChatBrowseWindow::setServer(const XMPP::Jid &roomServer)
{
	d->server = roomServer;
	d->ui.le_server->setText(d->server.full());
	d->ui.le_server->setCursorPosition(0);

	// FIXME: we shouldn't do this here
	QMetaObject::invokeMethod(this, "onBrowse", Qt::QueuedConnection,
		Q_ARG(XMPP::Jid, d->server));
}

void PsiGroupChatBrowseWindow::setServerVisible(bool b)
{
	d->ui.lb_server->setVisible(b);
	d->ui.le_server->setVisible(b);
	d->ui.pb_browse->setVisible(b);
}

void PsiGroupChatBrowseWindow::setNicknameVisible(bool b)
{
	d->ui.lb_nick->setVisible(b);
	d->ui.le_nick->setVisible(b);
}

void PsiGroupChatBrowseWindow::handleBrowseResultsReady(const QList<GroupChatBrowseWindow::RoomInfo> &list)
{
	d->model->addRooms(list);
}

void PsiGroupChatBrowseWindow::handleBrowseError(const QString &reason)
{
	// TODO
	Q_UNUSED(reason);
}

void PsiGroupChatBrowseWindow::handleJoinSuccess()
{
	close();
}

void PsiGroupChatBrowseWindow::handleJoinError(const QString &reason)
{
	d->setWidgetsEnabled(true);

	QMessageBox::information(this, tr("Error"), tr("Unable to join groupchat.\nReason: %1").arg(reason));
}

void PsiGroupChatBrowseWindow::handleCreateSuccess(const GroupChatBrowseWindow::RoomOptions &defaultOptions)
{
	QMetaObject::invokeMethod(this, "onCreateConfirm", Qt::QueuedConnection,
		Q_ARG(GroupChatBrowseWindow::RoomOptions, defaultOptions));
}

void PsiGroupChatBrowseWindow::handleCreateConfirmed()
{
	QMetaObject::invokeMethod(d, "createFinalize", Qt::QueuedConnection);
}

void PsiGroupChatBrowseWindow::handleCreateError(const QString &reason)
{
	d->setWidgetsEnabled(true);

	QMessageBox::information(this, tr("Error"), tr("Unable to create groupchat.\nReason: %1").arg(reason));
}

void PsiGroupChatBrowseWindow::handleDestroySuccess()
{
	d->setWidgetsEnabled(true);

	d->roomDestroyed();
}

void PsiGroupChatBrowseWindow::handleDestroyError(const QString &reason)
{
	d->setWidgetsEnabled(true);

	QMessageBox::information(this, tr("Error"), tr("Unable to destroy groupchat.\nReason: %1").arg(reason));
}

#include "groupchatbrowsewindow.moc"