#pragma once

#include "domain/Group.h"

#include <QDialog>
#include <QVector>

class QCheckBox;

namespace mini {

// S23 AdminRights: the 12-bit permission mask from §30.4.7.
class AdminRightsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AdminRightsDialog(QWidget *parent = nullptr);

    void setPermissions(quint32 permissions);
    [[nodiscard]] quint32 permissions() const;

    [[nodiscard]] QVector<QCheckBox *> boxes() const { return m_boxes; }

private:
    QVector<QCheckBox *> m_boxes;
};

} // namespace mini
