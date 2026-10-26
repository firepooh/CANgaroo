/*

  Copyright (c) 2026 jypark7

  This file is part of cangaroo.

  cangaroo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  cangaroo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cangaroo.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "McpGuideDialog.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "remote/McpController.h"

namespace
{
    // A read-only line with the text and a Copy button.
    QWidget *copyLine(const QString &text, QWidget *parent)
    {
        auto *row = new QWidget(parent);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);

        auto *edit = new QLineEdit(text, row);
        edit->setReadOnly(true);
        edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        edit->setCursorPosition(0);
        edit->setMinimumWidth(560);

        auto *copy = new QPushButton(QObject::tr("Copy"), row);
        QObject::connect(copy, &QPushButton::clicked, row, [text, copy]()
        {
            QGuiApplication::clipboard()->setText(text);
            copy->setText(QObject::tr("Copied"));
        });

        layout->addWidget(edit, 1);
        layout->addWidget(copy);
        return row;
    }

    QLabel *wrapped(const QString &text, QWidget *parent)
    {
        auto *label = new QLabel(text, parent);
        label->setWordWrap(true);
        label->setTextFormat(Qt::RichText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    }
}

McpGuideDialog::McpGuideDialog(McpController &mcp, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("MCP Guide"));
    auto *layout = new QVBoxLayout(this);

    QString state = mcp.statusText().toHtmlEscaped();
    if (!mcp.lastProblem().isEmpty())
    {
        state += "<br><span style='color:#c62828'>" + mcp.lastProblem().toHtmlEscaped() + "</span>";
    }
    layout->addWidget(wrapped(tr("<b>State:</b> %1").arg(state), this));

    layout->addWidget(wrapped(tr("<b>1. Register cangaroo in Claude Code</b> (once, in a terminal). "
                                 "Claude Code stores it per scope: the default <i>local</i> scope applies to the "
                                 "folder where you run the command, add <code>-s user</code> for every folder."), this));
    layout->addWidget(copyLine(mcp.registrationCommand(), this));

    layout->addWidget(wrapped(tr("<b>2. Start a new Claude Code session</b> so it loads the tools. "
                                 "The MCP endpoint is:"), this));
    layout->addWidget(copyLine(mcp.endpoint(), this));

    layout->addWidget(wrapped(tr("<b>3. Set the interfaces and press Start in cangaroo.</b> "
                                 "Most tools need a running measurement."), this));

    layout->addWidget(wrapped(tr("<b>AI Read-only</b> (MCP menu) blocks every transmit tool; reading frames and "
                                 "the bus status keep working. The HTTP server only listens on 127.0.0.1."), this));

    layout->addWidget(wrapped(tr("<b>Without HTTP</b> (Claude Code starts the MCP server itself, stdio). "
                                 "cangaroo still has to run with MCP enabled:"), this));
    layout->addWidget(copyLine(mcp.stdioRegistrationCommand(), this));

    if (mcp.httpState() == McpController::HttpState::PythonMissing)
    {
        layout->addWidget(wrapped(tr("<b>MCP server not found.</b> Create its Python environment once, "
                                     "in the GUI folder:"), this));
        layout->addWidget(copyLine(QStringLiteral("python -m venv mcp\\.venv && "
                                                  "mcp\\.venv\\Scripts\\python.exe -m pip install -r mcp\\requirements.txt"),
                                   this));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
