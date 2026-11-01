#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class Player;
class Settings;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QStackedWidget;
class QTableWidget;

class RadioView : public QWidget {
    Q_OBJECT
public:
    struct Station {
        QString name;
        QString url;
        QString category;
        QString cover;
        bool custom = false;
    };

    explicit RadioView(Settings *settings, Player *player, QWidget *parent = nullptr);
    bool playLibrary(bool shuffle);

private:
    void reload();
    void applyFilter();
    void applyShape();
    void selectRow(int row);
    void playRow(int row);
    void addStation();
    void removeStation();
    QString selectedCategory() const;

    Settings *settings_ = nullptr;
    Player *player_ = nullptr;
    class RadioCoverModel *coverModel_ = nullptr;
    QListView *grid_ = nullptr;
    QTableWidget *table_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *urlEdit_ = nullptr;
    QComboBox *categoryBox_ = nullptr;
    QLabel *status_ = nullptr;
    QList<QPushButton *> categoryButtons_;
    QPushButton *coversButton_ = nullptr;
    QPushButton *listButton_ = nullptr;
    QList<Station> stations_;
    QList<Station> shown_;
    int current_ = -1;
    bool covers_ = true;
    QString category_ = QStringLiteral("All");
    bool picking_ = false;
};
