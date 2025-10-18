#include "RepositoryWindow.h"

RepositoryWindow::RepositoryWindow(RepositoryManager& repoManager)
    : repositoryManager(repoManager)
{
    // Setup repository controls
    addAndMakeVisible(repositoriesLabel);
    repositoriesLabel.setText("JSFX Repositories", juce::dontSendNotification);
    repositoriesLabel.setFont(juce::FontOptions(16.0f).withStyle("Bold"));

    addAndMakeVisible(manageReposButton);
    manageReposButton.setButtonText("Manage Repositories...");
    manageReposButton.onClick = [this]() { showRepositoryEditor(); };

    addAndMakeVisible(refreshButton);
    refreshButton.setButtonText("Refresh");
    refreshButton.onClick = [this]() { refreshRepositoryList(); };

    // Setup package list
    addAndMakeVisible(packagesLabel);
    packagesLabel.setText("Available JSFX", juce::dontSendNotification);
    packagesLabel.setFont(juce::FontOptions(16.0f).withStyle("Bold"));

    addAndMakeVisible(packageList);
    packageList.setModel(this);
    packageList.setColour(juce::ListBox::backgroundColourId, juce::Colours::white);
    packageList.setColour(juce::ListBox::outlineColourId, juce::Colours::grey);
    packageList.setOutlineThickness(1);
    packageList.setRowHeight(60);

    addAndMakeVisible(installButton);
    installButton.setButtonText("Install Selected");
    installButton.setEnabled(false);
    installButton.onClick = [this]() { installSelectedPackage(); };

    addAndMakeVisible(installAllButton);
    installAllButton.setButtonText("Install All");
    installAllButton.setEnabled(false);
    installAllButton.onClick = [this]() { installAllPackages(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setText("", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);

    setSize(600, 600); // Narrower width to fit controls

    // Start loading repositories
    refreshRepositoryList();
}

RepositoryWindow::~RepositoryWindow()
{
    stopTimer();
    packageList.setModel(nullptr);
}

void RepositoryWindow::visibilityChanged()
{
    // Refresh installation status when window becomes visible
    if (isVisible() && !allPackages.empty())
    {
        DBG("RepositoryWindow became visible - refreshing installation status");
        packageList.repaint();
    }
}

void RepositoryWindow::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void RepositoryWindow::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Top controls
    auto topBar = bounds.removeFromTop(30);
    repositoriesLabel.setBounds(topBar.removeFromLeft(200));
    topBar.removeFromLeft(10);
    manageReposButton.setBounds(topBar.removeFromLeft(150));
    topBar.removeFromLeft(5);
    refreshButton.setBounds(topBar.removeFromLeft(80));

    bounds.removeFromTop(10);

    // Package list label
    packagesLabel.setBounds(bounds.removeFromTop(25));
    bounds.removeFromTop(5);

    // Status at bottom
    auto statusBar = bounds.removeFromBottom(25);
    statusLabel.setBounds(statusBar);
    bounds.removeFromBottom(5);

    // Install button
    auto buttonBar = bounds.removeFromBottom(30);
    installAllButton.setBounds(buttonBar.removeFromRight(100));
    buttonBar.removeFromRight(5);
    installButton.setBounds(buttonBar.removeFromRight(150));
    bounds.removeFromBottom(10);

    // Package list
    packageList.setBounds(bounds);
}

int RepositoryWindow::getNumRows()
{
    return static_cast<int>(allPackages.size());
}

void RepositoryWindow::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(allPackages.size()))
        return;

    const auto& package = allPackages[rowNumber];

    // Background
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue);
    else if (rowNumber % 2 == 0)
        g.fillAll(juce::Colours::white);
    else
        g.fillAll(juce::Colour(0xfff8f8f8));

    // Border
    g.setColour(juce::Colours::lightgrey);
    g.drawLine(0, height - 1, width, height - 1);

    auto bounds = juce::Rectangle<int>(5, 0, width - 10, height);

    // Package name
    g.setColour(juce::Colours::black);
    g.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    auto nameArea = bounds.removeFromTop(18);
    g.drawText(package.name, nameArea, juce::Justification::left);

    // Category and author
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(juce::Colours::darkgrey);
    auto metaArea = bounds.removeFromTop(15);
    juce::String meta = package.category + " • " + package.author + " • v" + package.version;
    g.drawText(meta, metaArea, juce::Justification::left);

    // Description
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(juce::Colours::darkgrey);
    auto descArea = bounds.removeFromTop(25);
    auto description = package.description.substring(0, 100);
    if (package.description.length() > 100)
        description += "...";
    g.drawText(description, descArea, juce::Justification::topLeft, true);

    // Installation status
    if (repositoryManager.isPackageInstalled(package))
    {
        g.setColour(juce::Colours::green);
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        g.drawText("INSTALLED", bounds, juce::Justification::bottomRight);
    }
}

void RepositoryWindow::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    // If clicking the same item again, deselect it
    if (selectedPackageIndex == row)
    {
        selectedPackageIndex = -1;
        packageList.deselectAllRows();

        // When nothing selected, always show Install All
        installButton.setButtonText("Install All");
        installButton.setEnabled(!allPackages.empty());
        updateInstallAllButtonText();
        return;
    }

    selectedPackageIndex = row;

    if (row >= 0 && row < static_cast<int>(allPackages.size()))
    {
        const auto& package = allPackages[row];
        bool isInstalled = repositoryManager.isPackageInstalled(package);

        // Only show Uninstall All when an installed package is selected
        installButton.setButtonText(isInstalled ? "Uninstall All" : "Install All");
        installButton.setEnabled(true);
    }
    else
    {
        installButton.setEnabled(false);
    }

    // Update Install All button text based on whether all packages are installed
    updateInstallAllButtonText();
}

void RepositoryWindow::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int>(allPackages.size()))
        return;

    selectedPackageIndex = row;
    const auto& package = allPackages[row];

    // Double-click on installed package = uninstall, otherwise install
    if (repositoryManager.isPackageInstalled(package))
    {
        installButton.setButtonText("Uninstall Selected");
        uninstallSelectedPackage();
    }
    else
    {
        installButton.setButtonText("Install Selected");
        installSelectedPackage();
    }
}

void RepositoryWindow::timerCallback()
{
    // Update status label with animated dots while loading
    if (isLoading)
    {
        static int dots = 0;
        dots = (dots + 1) % 4;
        juce::String loadingText = "Loading repositories";
        for (int i = 0; i < dots; ++i)
            loadingText += ".";
        statusLabel.setText(loadingText, juce::dontSendNotification);
    }
    else
    {
        // Not loading anymore, stop the timer
        stopTimer();
    }
}

void RepositoryWindow::refreshRepositoryList()
{
    // Deselect any selected item and show Install All
    selectedPackageIndex = -1;
    packageList.deselectAllRows();
    installButton.setButtonText("Install All");
    installButton.setEnabled(false); // Will be enabled after packages load

    isLoading = true;
    statusLabel.setText("Loading repositories...", juce::dontSendNotification);
    refreshButton.setEnabled(false);
    startTimer(500);

    repositories.clear();
    allPackages.clear();
    packageList.updateContent();

    auto urls = repositoryManager.getRepositoryUrls();
    if (urls.isEmpty())
    {
        isLoading = false;
        stopTimer();
        statusLabel.setText(
            "No repositories configured. Click 'Manage Repositories' to add some.",
            juce::dontSendNotification
        );
        refreshButton.setEnabled(true);
        return;
    }

    // Fetch all repositories
    auto remaining = std::make_shared<std::atomic<int>>(urls.size());

    for (const auto& url : urls)
    {
        repositoryManager.fetchRepository(
            url,
            [this, remaining](RepositoryManager::Repository repo, juce::String error)
            {
                if (error.isEmpty() && repo.isValid)
                {
                    repositories.push_back(repo);

                    // Add all packages from this repository
                    for (const auto& package : repo.packages)
                        allPackages.push_back(package);

                    packageList.updateContent();
                }

                int count = --(*remaining);
                if (count == 0)
                {
                    // All repositories loaded
                    isLoading = false;

                    juce::String status = juce::String(allPackages.size())
                                        + " JSFX available from "
                                        + juce::String(repositories.size())
                                        + " repositor"
                                        + (repositories.size() == 1 ? "y" : "ies");
                    statusLabel.setText(status, juce::dontSendNotification);
                    refreshButton.setEnabled(true);
                    installAllButton.setEnabled(!allPackages.empty());

                    // Enable Install All button when nothing selected
                    if (selectedPackageIndex < 0 && !allPackages.empty())
                    {
                        installButton.setButtonText("Install All");
                        installButton.setEnabled(true);
                    }

                    // Force repaint to update installation status badges
                    packageList.repaint();
                }
            }
        );
    }
}

void RepositoryWindow::refreshPackageList()
{
    packageList.updateContent();
    packageList.repaint();        // Force repaint to update INSTALLED badges
    updateInstallAllButtonText(); // Update button text based on installation status
}

void RepositoryWindow::installSelectedPackage()
{
    // If nothing selected, trigger install/uninstall all
    if (selectedPackageIndex < 0)
    {
        if (installButton.getButtonText() == "Uninstall All")
            uninstallAllPackages();
        else
            installAllPackages();
        return;
    }

    if (selectedPackageIndex >= static_cast<int>(allPackages.size()))
        return;

    const auto& package = allPackages[selectedPackageIndex];

    // Check if package is installed and button says "Uninstall"
    if (repositoryManager.isPackageInstalled(package) && installButton.getButtonText() == "Uninstall Selected")
    {
        uninstallSelectedPackage();
        return;
    }

    // Check if already installed
    if (repositoryManager.isPackageInstalled(package))
    {
        auto* topLevel = getTopLevelComponent();
        auto choice = juce::NativeMessageBox::showYesNoBox(
            juce::MessageBoxIconType::QuestionIcon,
            "Package Already Installed",
            package.name + " is already installed. Do you want to reinstall it?",
            topLevel,
            nullptr
        );

        if (choice == 0) // No
            return;
    }

    statusLabel.setText("Installing " + package.name + "...", juce::dontSendNotification);
    installButton.setEnabled(false);

    repositoryManager.installPackage(
        package,
        [this, package](bool success, juce::String message)
        {
            statusLabel.setText(message, juce::dontSendNotification);
            installButton.setEnabled(true);
            packageList.repaintRow(selectedPackageIndex);

            // Update button text after installation
            if (success)
            {
                installButton.setButtonText("Uninstall Selected");
                updateInstallAllButtonText();
            }

            if (success)
            {
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Installation Complete",
                    message + "\n\nYou can now load this JSFX from the file browser.",
                    this,
                    nullptr
                );
            }
            else
            {
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Installation Failed",
                    message,
                    this,
                    nullptr
                );
            }
        }
    );
}

void RepositoryWindow::installAllPackages()
{
    if (allPackages.empty())
    {
        DBG("Install All: No packages available");
        return;
    }

    // Check if button says "Uninstall All"
    if (installAllButton.getButtonText() == "Uninstall All")
    {
        uninstallAllPackages();
        return;
    }

    int toInstall = static_cast<int>(allPackages.size());

    DBG("Install All: Will install/update " << toInstall << " packages");

    // Confirm installation - using async version to avoid blocking
    // Get the top-level window to ensure dialog appears in front
    auto* topLevel = getTopLevelComponent();

    DBG("Showing install confirmation dialog for " << toInstall << " packages");

    // Use async message box with proper modal callback
    juce::MessageBoxOptions options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::QuestionIcon)
        .withTitle("Install All Packages")
        .withMessage("Install/update all " + juce::String(toInstall) + " package" + (toInstall == 1 ? "" : "s") + "?\n\n"
                    "This will install new packages and update existing ones.")
        .withButton("Install")
        .withButton("Cancel")
        .withAssociatedComponent(topLevel);

    juce::NativeMessageBox::showAsync(
        options,
        [this, toInstall](int result)
        {
            DBG("Dialog result: " << result << " (0=Install, 1=Cancel)");

            if (result != 0) // Not the Install button (first button = 0)
            {
                DBG("User cancelled install all");
                return;
            }

            DBG("User confirmed install all for " << toInstall << " packages");

            // Proceed with installation
            proceedWithInstallation();
        }
    );
}

void RepositoryWindow::proceedWithInstallation()
{
    // Install ALL packages (including already installed ones to update/reinstall)
    auto packagesToInstall = std::make_shared<std::vector<RepositoryManager::JSFXPackage>>();
    for (const auto& package : allPackages)
        packagesToInstall->push_back(package);

    int toInstall = static_cast<int>(packagesToInstall->size());

    DBG("proceedWithInstallation: Installing/updating " << toInstall << " packages");

    // Disable buttons during installation
    installButton.setEnabled(false);
    installAllButton.setEnabled(false);
    refreshButton.setEnabled(false);

    statusLabel.setText("Preparing to install " + juce::String(toInstall) + " packages...", juce::dontSendNotification);

    auto totalToInstall = packagesToInstall->size();
    auto installed = std::make_shared<std::atomic<int>>(0);
    auto failed = std::make_shared<std::atomic<int>>(0);

    DBG("Starting installation loop for " << totalToInstall << " packages");

    if (totalToInstall == 0)
    {
        DBG("ERROR: totalToInstall is 0!");
        installButton.setEnabled(true);
        installAllButton.setEnabled(true);
        refreshButton.setEnabled(true);
        return;
    }

    for (size_t i = 0; i < packagesToInstall->size(); ++i)
    {
        const auto& package = (*packagesToInstall)[i];
        DBG("Queueing installation " << (i + 1) << "/" << totalToInstall << ": " << package.name);

        repositoryManager.installPackage(
            package,
            [this, package, installed, failed, totalToInstall, i](bool success, juce::String message)
            {
                DBG("Callback received for " << package.name << " (item " << (i + 1) << "/" << totalToInstall << ")");

                if (success)
                {
                    (*installed)++;
                    DBG("Successfully installed: " << package.name);
                }
                else
                {
                    (*failed)++;
                    DBG("Failed to install: " << package.name << " - " << message);
                }

                int completed = (*installed) + (*failed);

                DBG("Progress: "
                    << completed
                    << "/"
                    << totalToInstall
                    << " (installed: "
                    << *installed
                    << ", failed: "
                    << *failed
                    << ")");

                statusLabel.setText(
                    "Installing... " + juce::String(completed) + "/" + juce::String(totalToInstall),
                    juce::dontSendNotification
                );

                if (completed >= static_cast<int>(totalToInstall))
                {
                    DBG("All installations complete. Installed: " << *installed << ", Failed: " << *failed);

                    // All done
                    juce::String resultMessage =
                        "Installed: " + juce::String(*installed) + "\n" + "Failed: " + juce::String(*failed);

                    statusLabel.setText(
                        "Installation complete: "
                            + juce::String(*installed)
                            + " installed, "
                            + juce::String(*failed)
                            + " failed",
                        juce::dontSendNotification
                    );

                    installButton.setEnabled(true);
                    installAllButton.setEnabled(true);
                    refreshButton.setEnabled(true);
                    packageList.updateContent();
                    packageList.repaint();        // Update INSTALLED badges
                    updateInstallAllButtonText(); // Update button text

                    juce::NativeMessageBox::showMessageBoxAsync(
                        *failed > 0 ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                        "Installation Complete",
                        resultMessage,
                        this,
                        nullptr
                    );
                }
            }
        );
    }

    DBG("All " << totalToInstall << " installation requests queued");
}

void RepositoryWindow::uninstallSelectedPackage()
{
    if (selectedPackageIndex < 0 || selectedPackageIndex >= static_cast<int>(allPackages.size()))
        return;

    const auto& package = allPackages[selectedPackageIndex];
    auto installDir = repositoryManager.getPackageInstallDirectory(package);

    // Confirm uninstallation
    auto* topLevel = getTopLevelComponent();

    juce::MessageBoxOptions options = juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle("Uninstall Package")
                                          .withMessage(
                                              "Are you sure you want to uninstall "
                                              + package.name
                                              + "?\n\nThis will delete:\n"
                                              + installDir.getFullPathName()
                                          )
                                          .withButton("OK")
                                          .withButton("Cancel")
                                          .withAssociatedComponent(topLevel);

    juce::NativeMessageBox::showAsync(
        options,
        [this, package, installDir](int result)
        {
            if (result != 0) // Not OK button (first button = 0)
                return;

            // Delete the installation directory
            if (installDir.exists() && installDir.deleteRecursively())
            {
                statusLabel.setText("Uninstalled " + package.name, juce::dontSendNotification);
                packageList.repaintRow(selectedPackageIndex);

                // Update button text
                installButton.setButtonText("Install Selected");
                updateInstallAllButtonText();

                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    "Uninstallation Complete",
                    package.name + " has been removed.",
                    this,
                    nullptr
                );
            }
            else
            {
                statusLabel.setText("Failed to uninstall " + package.name, juce::dontSendNotification);
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Uninstallation Failed",
                    "Could not delete the installation directory:\n" + installDir.getFullPathName(),
                    this,
                    nullptr
                );
            }
        }
    );
}

void RepositoryWindow::uninstallAllPackages()
{
    if (allPackages.empty())
        return;

    int installedCount = 0;
    for (const auto& package : allPackages)
        if (repositoryManager.isPackageInstalled(package))
            installedCount++;

    if (installedCount == 0)
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No Packages Installed",
            "There are no installed packages to uninstall.",
            this,
            nullptr
        );
        return;
    }

    // Confirm uninstallation
    auto* topLevel = getTopLevelComponent();
    juce::MessageBoxOptions options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Uninstall All Packages")
        .withMessage("Uninstall all " + juce::String(installedCount) + " installed package" + (installedCount == 1 ? "" : "s") + "?\n\n"
                    "This will delete all installed JSFX files from this repository.")
        .withButton("OK")
        .withButton("Cancel")
        .withAssociatedComponent(topLevel);

    juce::NativeMessageBox::showAsync(
        options,
        [this, installedCount](int result)
        {
            if (result != 0) // Not the OK button (first button = 0)
                return;

            int uninstalled = 0;
            int failed = 0;

            for (const auto& package : allPackages)
            {
                if (repositoryManager.isPackageInstalled(package))
                {
                    auto installDir = repositoryManager.getPackageInstallDirectory(package);
                    if (installDir.exists() && installDir.deleteRecursively())
                        uninstalled++;
                    else
                        failed++;
                }
            }

            packageList.repaint();
            updateInstallAllButtonText();

            // Update selected button if needed
            if (selectedPackageIndex >= 0)
                installButton.setButtonText("Install Selected");

            juce::String message =
                "Uninstalled " + juce::String(uninstalled) + " package" + (uninstalled == 1 ? "" : "s");
            if (failed > 0)
                message += "\nFailed to uninstall " + juce::String(failed) + " package" + (failed == 1 ? "" : "s");

            statusLabel.setText(message, juce::dontSendNotification);

            juce::NativeMessageBox::showMessageBoxAsync(
                failed > 0 ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                "Uninstallation Complete",
                message,
                this,
                nullptr
            );
        }
    );
}

void RepositoryWindow::updateInstallAllButtonText()
{
    if (allPackages.empty())
    {
        installAllButton.setButtonText("Install All");
        return;
    }

    int installedCount = 0;
    for (const auto& package : allPackages)
        if (repositoryManager.isPackageInstalled(package))
            installedCount++;

    if (installedCount == static_cast<int>(allPackages.size()))
        installAllButton.setButtonText("Uninstall All");
    else if (installedCount > 0)
        installAllButton.setButtonText("Install/Update All");
    else
        installAllButton.setButtonText("Install All");
}

void RepositoryWindow::showRepositoryEditor()
{
    auto* editor = new RepositoryEditorDialog(
        repositoryManager,
        [this]()
        {
            // Refresh after editing
            refreshRepositoryList();
        }
    );

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(editor);
    options.dialogTitle = "Manage Repositories";
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);

    options.launchAsync();
}

//==============================================================================
// RepositoryEditorDialog
//==============================================================================

RepositoryEditorDialog::RepositoryEditorDialog(RepositoryManager& repoManager, std::function<void()> onClose)
    : repositoryManager(repoManager)
    , closeCallback(onClose)
{
    addAndMakeVisible(instructionsLabel);
    instructionsLabel.setText(
        "Enter ReaPack compatible JSFX repository URLs, one per line:\n"
        "Example: https://raw.githubusercontent.com/JoepVanlier/JSFX/master/index.xml",
        juce::dontSendNotification
    );
    instructionsLabel.setJustificationType(juce::Justification::topLeft);
    instructionsLabel.setFont(juce::FontOptions(12.0f));

    addAndMakeVisible(repositoryEditor);
    repositoryEditor.setMultiLine(true);
    repositoryEditor.setReturnKeyStartsNewLine(true);
    repositoryEditor.setScrollbarsShown(true);
    repositoryEditor.setFont(juce::FontOptions(12.0f));

    // Load current repositories
    auto urls = repositoryManager.getRepositoryUrls();
    repositoryEditor.setText(urls.joinIntoString("\n"));

    addAndMakeVisible(saveButton);
    saveButton.setButtonText("Save");
    saveButton.onClick = [this]() { saveAndClose(); };

    addAndMakeVisible(cancelButton);
    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this]() { cancel(); };

    setSize(600, 400);
}

RepositoryEditorDialog::~RepositoryEditorDialog() = default;

void RepositoryEditorDialog::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void RepositoryEditorDialog::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    instructionsLabel.setBounds(bounds.removeFromTop(40));
    bounds.removeFromTop(5);

    auto buttonBar = bounds.removeFromBottom(30);
    cancelButton.setBounds(buttonBar.removeFromRight(80));
    buttonBar.removeFromRight(5);
    saveButton.setBounds(buttonBar.removeFromRight(80));
    bounds.removeFromBottom(10);

    repositoryEditor.setBounds(bounds);
}

void RepositoryEditorDialog::saveAndClose()
{
    // Parse URLs from text editor
    juce::StringArray urls;
    auto lines = juce::StringArray::fromLines(repositoryEditor.getText());

    for (const auto& line : lines)
    {
        auto trimmed = line.trim();
        if (trimmed.isNotEmpty() && !trimmed.startsWith("#"))
            urls.add(trimmed);
    }

    repositoryManager.setRepositoryUrls(urls);

    if (closeCallback)
        closeCallback();

    // Close dialog
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(1);
}

void RepositoryEditorDialog::cancel()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(0);
}
