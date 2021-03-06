#include "FavouriteTools.h"
#include "ui_FavouriteTools.h"
#include "Bridge.h"
#include "BrowseDialog.h"
#include "MainWindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include "MiscUtil.h"

FavouriteTools::FavouriteTools(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::FavouriteTools)
{
    ui->setupUi(this);
    //set window flags
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);

    setupTools("Tool", ui->listTools);
    setupTools("Script", ui->listScript);

    QStringList tblHeaderCommand;
    tblHeaderCommand << tr("Command") << tr("Shortcut");
    QTableWidget* list = ui->listCommand;
    list->setColumnCount(2);
    list->verticalHeader()->setVisible(false);
    list->setHorizontalHeaderLabels(tblHeaderCommand);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setShowGrid(false);
    list->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->verticalHeader()->setDefaultSectionSize(15);
    char buffer[MAX_SETTING_SIZE];
    int i;
    std::vector<QString> allCommand;
    std::vector<QString> allToolShortcut;
    for(i = 1; BridgeSettingGet("Favourite", QString("Command%1").arg(i).toUtf8().constData(), buffer); i++)
    {
        QString command = QString::fromUtf8(buffer);
        QString commandShortcut("");
        if(BridgeSettingGet("Favourite", QString("CommandShortcut%1").arg(i).toUtf8().constData(), buffer))
            commandShortcut = QString::fromUtf8(buffer);
        allCommand.push_back(command);
        allToolShortcut.push_back(commandShortcut);
    }
    i--;
    if(!allCommand.empty())
    {
        list->setRowCount(i);
        for(int j = 0; j < i; j++)
        {
            list->setItem(j, 0, new QTableWidgetItem(allCommand.at(j)));
            list->setItem(j, 1, new QTableWidgetItem(allToolShortcut.at(j)));
        }
    }
    originalToolsCount = ui->listTools->rowCount();
    originalScriptCount = ui->listScript->rowCount();
    originalCommandCount = ui->listCommand->rowCount();
    ui->listTools->selectRow(0);
    ui->listScript->selectRow(0);
    ui->listCommand->selectRow(0);
    connect(ui->listTools, SIGNAL(itemSelectionChanged()), this, SLOT(onListSelectionChanged()));
    connect(ui->listScript, SIGNAL(itemSelectionChanged()), this, SLOT(onListSelectionChanged()));
    connect(ui->listCommand, SIGNAL(itemSelectionChanged()), this, SLOT(onListSelectionChanged()));
}

void FavouriteTools::setupTools(QString name, QTableWidget* list)
{
    QStringList tblHeaderTools;
    tblHeaderTools << tr("Path") << tr("Shortcut") << tr("Description");
    list->setColumnCount(3);
    list->verticalHeader()->setVisible(false);
    list->setHorizontalHeaderLabels(tblHeaderTools);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setShowGrid(false);
    list->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->verticalHeader()->setDefaultSectionSize(15);

    char buffer[MAX_SETTING_SIZE];
    int i;
    std::vector<QString> allToolPath;
    std::vector<QString> allToolShortcut;
    std::vector<QString> allToolDescription;
    for(i = 1; BridgeSettingGet("Favourite", (name + QString::number(i)).toUtf8().constData(), buffer); i++)
    {
        QString toolPath = QString::fromUtf8(buffer);
        QString toolShortcut("");
        QString toolDescription("");
        if(BridgeSettingGet("Favourite", (name + "Shortcut" + QString::number(i)).toUtf8().constData(), buffer))
            toolShortcut = QString::fromUtf8(buffer);
        if(BridgeSettingGet("Favourite", (name + "Description" + QString::number(i)).toUtf8().constData(), buffer))
            toolDescription = QString::fromUtf8(buffer);
        allToolPath.push_back(toolPath);
        allToolShortcut.push_back(toolShortcut);
        allToolDescription.push_back(toolDescription);
    }
    i--;
    if(!allToolPath.empty())
    {
        list->setRowCount(i);
        for(int j = 0; j < i; j++)
        {
            list->setItem(j, 0, new QTableWidgetItem(allToolPath.at(j)));
            list->setItem(j, 1, new QTableWidgetItem(allToolShortcut.at(j)));
            list->setItem(j, 2, new QTableWidgetItem(allToolDescription.at(j)));
        }
    }
}

// Events
void FavouriteTools::on_btnAddFavouriteTool_clicked()
{
    QString filename;
    char buffer[MAX_SETTING_SIZE];
    memset(buffer, 0, sizeof(buffer));
    BridgeSettingGet("Favourite", "LastToolPath", buffer);
    BrowseDialog browse(this, QString("Browse tool"), QString("Enter the path of the tool."), QString("Executable Files (*.exe);;All Files (*.*)"), QString::fromUtf8(buffer), false);
    if(browse.exec() != QDialog::Accepted)
        return;
    filename = browse.path;
    BridgeSettingSet("Favourite", "LastToolPath", filename.toUtf8().constData());
    int rows = ui->listTools->rowCount();
    ui->listTools->setRowCount(rows + 1);
    ui->listTools->setItem(rows, 0, new QTableWidgetItem(filename));
    ui->listTools->setItem(rows, 1, new QTableWidgetItem(QString()));
    ui->listTools->setItem(rows, 2, new QTableWidgetItem(filename));
    if(rows == 0)
        ui->listTools->selectRow(0);
}

void FavouriteTools::upbutton(QTableWidget* table)
{
    if(!table->rowCount())
        return;
    int currentRow = table->currentRow();
    if(currentRow == 0)
        return;
    for(int i = 0; i < table->columnCount(); i++)
    {
        QString prevContent(table->item(currentRow, i)->text());
        table->item(currentRow, i)->setText(table->item(currentRow - 1, i)->text());
        table->item(currentRow - 1, i)->setText(prevContent);
    }
    table->selectRow(currentRow - 1);
}

void FavouriteTools::downbutton(QTableWidget* table)
{
    if(!table->rowCount())
        return;
    int currentRow = table->currentRow();
    if(currentRow == table->rowCount() - 1)
        return;
    for(int i = 0; i < table->columnCount(); i++)
    {
        QString prevContent(table->item(currentRow, i)->text());
        table->item(currentRow, i)->setText(table->item(currentRow + 1, i)->text());
        table->item(currentRow + 1, i)->setText(prevContent);
    }
    table->selectRow(currentRow + 1);
}

void FavouriteTools::on_btnRemoveFavouriteTool_clicked()
{
    QTableWidget* table = ui->listTools;
    if(!table->rowCount())
        return;
    table->removeRow(table->currentRow());
}

void FavouriteTools::on_btnDescriptionFavouriteTool_clicked()
{
    QTableWidget* table = ui->listTools;
    if(!table->rowCount())
        return;
    QString description = table->item(table->currentRow(), 2)->text();
    if(SimpleInputBox(this, tr("Enter the description"), description, description))
        table->item(table->currentRow(), 2)->setText(description);
}

void FavouriteTools::on_btnUpFavouriteTool_clicked()
{
    upbutton(ui->listTools);
}

void FavouriteTools::on_btnDownFavouriteTool_clicked()
{
    downbutton(ui->listTools);
}

void FavouriteTools::on_btnAddFavouriteScript_clicked()
{
    QString filename;
    char buffer[MAX_SETTING_SIZE];
    memset(buffer, 0, sizeof(buffer));
    BridgeSettingGet("Favourite", "LastScriptPath", buffer);
    filename = QFileDialog::getOpenFileName(this, tr("Select script"), QString::fromUtf8(buffer), tr("Script files (*.txt *.scr);;All files (*.*)"));
    if(filename.size() == 0)
        return;
    BridgeSettingSet("Favourite", "LastScriptPath", filename.toUtf8().constData());
    int rows = ui->listScript->rowCount();
    ui->listScript->setRowCount(rows + 1);
    ui->listScript->setItem(rows, 0, new QTableWidgetItem(filename));
    ui->listScript->setItem(rows, 1, new QTableWidgetItem(QString("NOT_SET")));
    ui->listScript->setItem(rows, 2, new QTableWidgetItem(filename));
    if(rows == 0)
        ui->listScript->selectRow(0);
}

void FavouriteTools::on_btnRemoveFavouriteScript_clicked()
{
    QTableWidget* table = ui->listScript;
    if(!table->rowCount())
        return;
    table->removeRow(table->currentRow());
}

void FavouriteTools::on_btnDescriptionFavouriteScript_clicked()
{
    QTableWidget* table = ui->listScript;
    if(!table->rowCount())
        return;
    QString description = table->item(table->currentRow(), 2)->text();
    if(SimpleInputBox(this, tr("Enter the description"), description, description))
        table->item(table->currentRow(), 2)->setText(description);
}

void FavouriteTools::on_btnUpFavouriteScript_clicked()
{
    upbutton(ui->listScript);
}

void FavouriteTools::on_btnDownFavouriteScript_clicked()
{
    downbutton(ui->listScript);
}

void FavouriteTools::on_btnAddFavouriteCommand_clicked()
{
    QString cmd;
    if(SimpleInputBox(this, tr("Enter the command that you want to create a shortcut for :"), "", cmd))
    {
        int rows = ui->listCommand->rowCount();
        ui->listCommand->setRowCount(rows + 1);
        ui->listCommand->setItem(rows, 0, new QTableWidgetItem(cmd));
        ui->listCommand->setItem(rows, 1, new QTableWidgetItem(QString("NOT_SET")));
        if(rows == 0)
            ui->listCommand->selectRow(0);
    }
}

void FavouriteTools::on_btnRemoveFavouriteCommand_clicked()
{
    QTableWidget* table = ui->listCommand;
    if(!table->rowCount())
        return;
    table->removeRow(table->currentRow());
}

void FavouriteTools::on_btnUpFavouriteCommand_clicked()
{
    upbutton(ui->listCommand);
}

void FavouriteTools::on_btnDownFavouriteCommand_clicked()
{
    downbutton(ui->listCommand);
}

void FavouriteTools::onListSelectionChanged()
{
    QTableWidget* table = qobject_cast<QTableWidget*>(sender());
    if(table == nullptr)
        return;
    QModelIndexList indexes = table->selectionModel()->selectedRows();
    if(indexes.count() < 1)
        return;
    ui->shortcutEdit->setErrorState(false);
    currentShortcut = QKeySequence(table->item(table->currentRow(), 1)->text());
    ui->shortcutEdit->setText(currentShortcut.toString(QKeySequence::NativeText));
}

void FavouriteTools::on_shortcutEdit_askForSave()
{
    QTableWidget* table;
    switch(ui->tabWidget->currentIndex())
    {
    case 0:
        table = ui->listTools;
        break;
    case 1:
        table = ui->listScript;
        break;
    case 2:
        table = ui->listCommand;
        break;
    default:
        return;
    }
    if(!table->rowCount())
        return;
    const QKeySequence newKey = ui->shortcutEdit->getKeysequence();
    if(newKey != currentShortcut)
    {
        bool good = true;
        if(!newKey.isEmpty())
        {
            for(auto i = Config()->Shortcuts.cbegin(); i != Config()->Shortcuts.cend(); ++i)
            {
                if(i.value().Hotkey == newKey) //newkey is trying to override a global shortcut
                {
                    good = false;
                    break;
                }
            }
        }
        if(good)
        {
            QString keyText = "";
            if(!newKey.isEmpty())
                keyText = newKey.toString(QKeySequence::NativeText);
            table->item(table->currentRow(), 1)->setText(keyText);
            ui->shortcutEdit->setErrorState(false);
        }
        else
        {
            ui->shortcutEdit->setErrorState(true);
        }
    }
}

void FavouriteTools::on_btnOK_clicked()
{
    for(int i = 1; i <= ui->listTools->rowCount(); i++)
    {
        BridgeSettingSet("Favourite", QString("Tool%1").arg(i).toUtf8().constData(), ui->listTools->item(i - 1, 0)->text().toUtf8().constData());
        BridgeSettingSet("Favourite", QString("ToolShortcut%1").arg(i).toUtf8().constData(), ui->listTools->item(i - 1, 1)->text().toUtf8().constData());
        BridgeSettingSet("Favourite", QString("ToolDescription%1").arg(i).toUtf8().constData(), ui->listTools->item(i - 1, 2)->text().toUtf8().constData());
    }
    if(ui->listTools->rowCount() == 0)
    {
        BridgeSettingSet("Favourite", "Tool1", "");
        BridgeSettingSet("Favourite", "ToolShortcut1", "");
        BridgeSettingSet("Favourite", "ToolDescription1", "");
    }
    else
        for(int i = ui->listTools->rowCount() + 1; i <= originalToolsCount; i++)
        {
            BridgeSettingSet("Favourite", QString("Tool%1").arg(i).toUtf8().constData(), "");
            BridgeSettingSet("Favourite", QString("ToolShortcut%1").arg(i).toUtf8().constData(), "");
            BridgeSettingSet("Favourite", QString("ToolDescription%1").arg(i).toUtf8().constData(), "");
        }
    for(int i = 1; i <= ui->listScript->rowCount(); i++)
    {
        BridgeSettingSet("Favourite", QString("Script%1").arg(i).toUtf8().constData(), ui->listScript->item(i - 1, 0)->text().toUtf8().constData());
        BridgeSettingSet("Favourite", QString("ScriptShortcut%1").arg(i).toUtf8().constData(), ui->listScript->item(i - 1, 1)->text().toUtf8().constData());
        BridgeSettingSet("Favourite", QString("ScriptDescription%1").arg(i).toUtf8().constData(), ui->listScript->item(i - 1, 2)->text().toUtf8().constData());
    }
    if(ui->listScript->rowCount() == 0)
    {
        BridgeSettingSet("Favourite", "Script1", "");
        BridgeSettingSet("Favourite", "ScriptShortcut1", "");
        BridgeSettingSet("Favourite", "ScriptDescription1", "");
    }
    else
        for(int i = ui->listScript->rowCount() + 1; i <= originalScriptCount; i++)
        {
            BridgeSettingSet("Favourite", QString("Script%1").arg(i).toUtf8().constData(), "");
            BridgeSettingSet("Favourite", QString("ScriptShortcut%1").arg(i).toUtf8().constData(), "");
            BridgeSettingSet("Favourite", QString("ScriptDescription%1").arg(i).toUtf8().constData(), "");
        }
    for(int i = 1; i <= ui->listCommand->rowCount(); i++)
    {
        BridgeSettingSet("Favourite", QString("Command%1").arg(i).toUtf8().constData(), ui->listCommand->item(i - 1, 0)->text().toUtf8().constData());
        BridgeSettingSet("Favourite", QString("CommandShortcut%1").arg(i).toUtf8().constData(), ui->listCommand->item(i - 1, 1)->text().toUtf8().constData());
    }
    if(ui->listCommand->rowCount() == 0)
    {
        BridgeSettingSet("Favourite", "Command1", "");
        BridgeSettingSet("Favourite", "CommandShortcut1", "");
    }
    else
        for(int i = ui->listCommand->rowCount() + 1; i <= originalCommandCount; i++)
        {
            BridgeSettingSet("Favourite", QString("Command%1").arg(i).toUtf8().constData(), "");
            BridgeSettingSet("Favourite", QString("CommandShortcut%1").arg(i).toUtf8().constData(), "");
        }
    this->done(QDialog::Accepted);
}

FavouriteTools::~FavouriteTools()
{
    delete ui;
}
