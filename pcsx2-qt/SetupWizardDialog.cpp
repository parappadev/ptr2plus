// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "pcsx2/SIO/Pad/Pad.h"
#include "QtHost.h"
#include "QtUtils.h"
#include "SettingWidgetBinder.h"
#include "Settings/BIOSSettingsWidget.h"
#include "Settings/ControllerSettingWidgetBinder.h"
#include "Settings/InterfaceSettingsWidget.h"
#include "SetupWizardDialog.h"

#include <QtWidgets/QMessageBox>
#include <common/FileSystem.h>
#include "common/Error.h"
//ptr2plus
#include "CDVD/CDVD.h"
#include <common/StringUtil.h>

#include <common/FileSystem.h>
#include <common/Path.h>
#include <common/StringUtil.h>
#include <pcsx2-qt/SetupWizardDialog.h>

bool SetupWizardDialog::askOverwrite(const std::string dest_path, bool& overwrite_set, bool& overwrite)
{
	QMessageBox msgBox;
	std::string msgBoxText = "Path " + std::string(Path::GetFileName(dest_path)) + " already exists. Overwrite?";
	msgBox.setWindowIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxQuestion));
	msgBox.setIcon(QMessageBox::Question);
	msgBox.setText(QString::fromStdString(msgBoxText));
	msgBox.setWindowTitle("Collision");
	msgBox.setDetailedText(QString::fromStdString(dest_path));
	msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll);
	msgBox.setDefaultButton(QMessageBox::Yes);
	int ret = msgBox.exec();
	switch (ret)
	{
		case QMessageBox::Yes:
			return true;
			break;
		case QMessageBox::YesToAll:
			overwrite_set = true;
			overwrite = true;
			return true;
			break;
		case QMessageBox::No:
			return false;
			break;
		case QMessageBox::NoToAll:
			overwrite_set = true;
			overwrite = false;
			return false;
			break;
		default: //shouldnt be reached
			break;
	}
	//shouldnt reach here
}

//adapted from pwf2int (unlicensed
#define readsrc(x) if(src >= srcend) { break; } (x) = *src++;
#define writedst(x) if(dst >= dstend) { break; } *dst++ = (x);
// lzss_decompress is partially derived from Haruhiko Okumura (4/6/1989), in which they express the freedom to use, modify, and distribute their "LZSS.C" source code.
void lzss_decompress(int EI, int EJ, int P, int rless, char* buffer, const char* srcstart, int srclen, char* dststart, int dstlen)
{
	int N = (1 << EI);
	int F = (1 << EJ);

	const char* src = srcstart;
	const char* srcend = srcstart + srclen;

	char* dst = dststart;
	char* dstend = dststart + dstlen;

	int r = (N - F) - rless;

	int flags;
	int c, i, j, k;
	const int NMASK = (N - 1);
	const int FMASK = (F - 1);
	for (flags = 0;; flags >>= 1)
	{
		if (!(flags & 0x100))
		{
			readsrc(flags);
			flags |= 0xFF00;
		}
		if (flags & 1)
		{
			readsrc(c);
			writedst(c);
			buffer[r++] = c;
			r &= NMASK;
		}
		else
		{
			readsrc(i);
			readsrc(j);
			i |= ((j >> EJ) << 8);
			j = (j & FMASK) + P;
			for (k = 0; k <= j; k += 1)
			{
				c = buffer[(i + k) & NMASK];
				writedst(c);
				buffer[r++] = c;
				r &= NMASK;
			}
		}
	}
}
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

#define N 4096 /* Size of ring buffer */
#define F 18 /* Upper limit */
#define THRESHOLD 2
#define lzss_read() *(fp_r)++;
#define lzss_write(x) *(fp_w)++ = (x);
  /* Ring buffer for INT decompression */

void PackIntDecodeWait(u_char* fp_r, u_char* fp_w){

	unsigned char RBuff[N + F - 1] = {};
	u_int moto_size; /* Decode size                                  */
	u_char* fp_w_end; /* End of write buffer (buffer + decode size)   */

	int rp = N - F; /* Ring buffer position                         */
	unsigned int flags = 0; /* LZSS flags                                   */

	int i, c, c1, c2;

	//printf("decode moto[%08x] saki[%08x]\n", fp_r, fp_w);
	//asm("sync.l");

	moto_size = *(u_int*)fp_r;
	//fp_w = (u_char*)PR_UNCACHED(fp_w);
	fp_w_end = fp_w + moto_size;

	fp_r += 8;

	for (i = 0; i < N - F; i++)
	{
		RBuff[i] = 0;
	}

	while (1)
	{
		if (fp_w > fp_w_end)
		{
			//printf(" over data pointer\n");
			break;
		}
		if (fp_w == fp_w_end)
		{
			break;
		}

		if (((flags >>= 1) & 256) == 0)
		{
			c = lzss_read();
			flags = c | 0xff00;
		}

		if (flags & 1)
		{
			c = lzss_read();
			lzss_write(c);

			RBuff[rp++] = c;
			rp &= (N - 1);
		}
		else
		{
			c1 = lzss_read();
			c2 = lzss_read();

			c1 |= ((c2 & 0xf0) << 4);
			c2 = (c2 & 0x0f) + THRESHOLD;

			for (i = 0; i <= c2; i++)
			{
				c = RBuff[(c1 + i) & (N - 1)];
				lzss_write(c);

				RBuff[rp++] = c;
				rp &= (N - 1);
			}
		}
	}
	return;
}

#define INT_RESOURCE_END 0
#define INT_RESOURCE_TM0 1
#define INT_RESOURCE_SOUNDS 2
#define INT_RESOURCE_STAGE 3
#define INT_RESOURCE_HATCOLORBASE 4

struct pack_int_header
{
	u32 magic;
	u32 filecount;
	u32 resourcetype;
	u32 fntableoffset;
	u32 fntablesizeinbytes;
	u32 lzss_section_size;
	u32 unk[2];
};

struct lzss_header_t
{
	u32 uncompressed_size;
	u32 compressed_size;
	//u8 data[0];
};

struct filename_entry_t
{
	u32 offset;
	u32 sizeof_file;
};

//int structure
//each folder:
//	int_header (32 bytes
//  file offset table (4 * filecount
//	fnt table (fnttablesizeinbytes
//	lzss_header (8 bytes
//	compressed data (compressed_size
static_assert(sizeof(pack_int_header) == 0x20, "pack_int_header struct not packed to 0x!F bytes");
bool SetupWizardDialog::extractINTArchive(const std::string int_path, bool& overwrite_set, bool& overwrite)
{
	const char* typenames[8] = {
		"END",
		"VRAM", //TEXTURES
		"SND", //SOUNDS
		"ONMEM", //PROPS
		"R1", //HAT_RED
		"R2", //HAT_BLUE
		"R3", //HAT_PINK
		"R4" //HAT_YELL
	};

	auto fp = FileSystem::OpenManagedCFile(int_path.c_str(), "rb+");
	if (!fp) {
		DisplayErrorMessage("Could not open INT file. (Check permissions?)", int_path);
		return false;
	}
	auto stream = fp.get();
	
	pack_int_header header;
	std::fread(&header, sizeof(pack_int_header), 1, stream);
	if (ferror(stream)) {
		DisplayErrorMessage("Problem reading INT file.", "Error reading header at offset: 0, path : " + int_path);
		return false;
	}
	if (header.magic != 0x44332211) {
		DisplayErrorMessage("Problem reading INT file.", "Header magic did not match at offset: 0, path : " + int_path);
		return false;
	}

	int folder_offset = 0;

	while (header.resourcetype != INT_RESOURCE_END)
	{
		const char* restypename = typenames[header.resourcetype]; // get the type

		lzss_header_t lzss;
		std::fseek(stream, folder_offset + header.fntableoffset + header.fntablesizeinbytes, SEEK_SET);
		std::fread(&lzss, sizeof(lzss), 1, stream); // get lzzs header
		if (ferror(stream)) {
			int offset = folder_offset + header.fntableoffset + header.fntablesizeinbytes;
			DisplayErrorMessage("Problem reading INT file.", "Error reading lzss header at offset: " + std::to_string(offset) + ", path: " + int_path);
			return false;
		}
		u_char* uncompressed_folder = (u_char*)(malloc(lzss.uncompressed_size));
		u_char* compressed_folder = (u_char*)(malloc(sizeof(u32) * 2 + lzss.compressed_size));

		*(int*)compressed_folder = lzss.uncompressed_size;
		*(int*)(compressed_folder + sizeof(u32)) = lzss.compressed_size;
		std::fread(compressed_folder + sizeof(u32) * 2, lzss.compressed_size, 1, stream);
		if (ferror(stream))
		{
			int offset = folder_offset + header.fntableoffset + header.fntablesizeinbytes + sizeof(lzss_header_t);
			DisplayErrorMessage("Problem reading INT file.", "Error reading compressed data at offset: " + std::to_string(offset) + ", path: " + int_path);
			return false;
		}
		//memset(history, 0, 4096); // set history to 0s
		//lzss_decompress(12, 4, 2, 2, history, compressed_folder, lzss.compressed_size, uncompressed_folder, lzss.uncompressed_size); // uncompress into uncompressed size
		PackIntDecodeWait(compressed_folder, uncompressed_folder);
		std::string extract_path = Path::Combine(Path::StripExtension(int_path), restypename);
		
		FileSystem::EnsureDirectoryExists(extract_path.c_str(), true);

		for (u32 i = 0; i < header.filecount; i ++) // iterate over every file
		{ 
			//get filename entry
			filename_entry_t entry; 
			std::fseek(stream, folder_offset + header.fntableoffset + sizeof(filename_entry_t) * i, SEEK_SET);
			std::fread(&entry, sizeof(filename_entry_t), 1, stream); // get name
			if (ferror(stream))
			{
				int offset = folder_offset + header.fntableoffset + sizeof(filename_entry_t) * i;
				DisplayErrorMessage("Problem reading INT file.", "Error reading filename_entry chunk at offset: " + std::to_string(offset) + ", path: " + int_path);
				return false;
			}
			//get filename
			std::fseek(stream, folder_offset + header.fntableoffset + sizeof(filename_entry_t) * header.filecount + entry.offset, SEEK_SET);
			char buf[900] = {};
			if (std::fgets(buf, sizeof(buf), stream) == nullptr)
			{
				int offset = folder_offset + header.fntableoffset + sizeof(filename_entry_t) * header.filecount + entry.offset;
				DisplayErrorMessage("Problem reading INT file.", "Error reading string filename_entry string at offset: " + std::to_string(offset) + ", path: " + int_path);
				return false;
			}

			std::string filename = buf;
			std::string tmp = Path::Combine(extract_path, filename);
			const char* extract_path_file = tmp.c_str();

			//get file uncompressed data offset
			u32 fileoffset;
			std::fseek(stream, folder_offset + sizeof(pack_int_header) + sizeof(u32) * i, SEEK_SET);
			std::fread(&fileoffset, sizeof(u32), 1, stream); // get offsets
			if (ferror(stream))
			{
				int offset = folder_offset + sizeof(pack_int_header) + sizeof(u32) * i;
				DisplayErrorMessage("Problem reading INT file.", "Error reading uncompressed file offset chunk at offset: " + std::to_string(offset) + ", path: " + extract_path_file);
				return false;
			}
			if (!FileSystem::PathExists(extract_path_file))
			{
				if (!FileSystem::WriteBinaryFile(extract_path_file, uncompressed_folder + fileoffset, entry.sizeof_file))
				{
					DisplayErrorMessage("Could not write file from INT archive. (Check space/permissions?)", extract_path_file);
					return false;
				}
			}
			else if (!overwrite_set)
			{
				if (askOverwrite(extract_path_file, overwrite_set, overwrite))
				{
					if (!FileSystem::DeletePath(extract_path_file))
					{
						DisplayErrorMessage("Could not overwrite existing file from INT archive. (Check permissions?)", extract_path_file);
						return false;
					}
					if (!FileSystem::WriteBinaryFile(extract_path_file, uncompressed_folder + fileoffset, entry.sizeof_file))
					{
						DisplayErrorMessage("Could not write file from INT archive. (Check space/permissions?)", extract_path_file);
						return false;
					}
				}
			}
			else if (overwrite)
			{
				if (!FileSystem::DeletePath(extract_path_file))
				{
					DisplayErrorMessage("Could not overwrite existing file from INT archive. (Check permissions?)", extract_path_file);
					return false;
				}
				if (!FileSystem::WriteBinaryFile(extract_path_file, uncompressed_folder + fileoffset, entry.sizeof_file))
				{
					DisplayErrorMessage("Could not write file from INT archive. (Check space/permissions?)", extract_path_file);
					return false;
				}
			}
		}
		// move to next section
		folder_offset += header.fntableoffset + header.fntablesizeinbytes + header.lzss_section_size; //update offset
		std::fseek(stream, folder_offset, SEEK_SET);
		std::fread(&header, sizeof(pack_int_header), 1, stream); //read new header
		if (ferror(stream))
		{
			DisplayErrorMessage("Problem reading INT file.", "Error reading header as offset: " + std::to_string(folder_offset) + ", path: " + int_path);
			return false;
		}
		if (header.magic != 0x44332211)
		{
			DisplayErrorMessage("Problem reading INT file.", "Header magic did not match at offset: 0, path : " + int_path);
			return false;
		}
		free(compressed_folder);
		free(uncompressed_folder); // free uncompressed data
	}
	return true;
}

SetupWizardDialog::SetupWizardDialog()
{
	setupUi();
	updatePageLabels(-1);
	updatePageButtons();
}

SetupWizardDialog::~SetupWizardDialog() = default;

void SetupWizardDialog::resizeEvent(QResizeEvent* event)
{
	QDialog::resizeEvent(event);
	//resizeDirectoryListColumns();
}

bool SetupWizardDialog::canShowNextPage()
{
	const int current_page = m_ui.pages->currentIndex();

	switch (current_page)
	{
		case Page_BIOS:
		{
			if (!m_ui.biosList->currentItem())
			{
				if (QMessageBox::question(this, tr("Warning"),
						tr("A BIOS image has not been selected. PCSX2 <strong>will not</strong> be able to run games "
						   "without a BIOS image.<br><br>Are you sure you wish to continue without selecting a BIOS "
						   "image?")) != QMessageBox::Yes)
				{
					return false;
				}
			}
		}
		break;

		case Page_PTR2:
		{
			if (m_ui.progressBar->value() == 0)
			{

				if (QMessageBox::warning(this, tr("Warning"),
						tr("Unable to proceed without an extracted ISO.")))
				{
					return false;
				}
			}
			else if (m_ui.progressBar->value() != 100) //and extraction in process
			{
				if (QMessageBox::warning(this, tr("Warning"),
						tr("Unable to proceed until extraction of the ISO is complete.")))
				{
					return false;
				}
			}


		}
		break;

		default:
			break;
	}

	return true;
}

void SetupWizardDialog::previousPage()
{
	const int current_page = m_ui.pages->currentIndex();
	if (current_page == 0)
		return;

	m_ui.pages->setCurrentIndex(current_page - 1);
	updatePageLabels(current_page);
	updatePageButtons();
}

void SetupWizardDialog::nextPage()
{
	const int current_page = m_ui.pages->currentIndex();
	if (current_page == Page_Complete)
	{
		accept();
		return;
	}

	if (!canShowNextPage())
		return;

	const int new_page = current_page + 1;
	m_ui.pages->setCurrentIndex(new_page);
	updatePageLabels(current_page);
	updatePageButtons();
	pageChangedTo(new_page);
}

void SetupWizardDialog::pageChangedTo(int page)
{
	switch (page)
	{
		case Page_PTR2:
			//resizeDirectoryListColumns();
			break;

		default:
			break;
	}
}

void SetupWizardDialog::updatePageLabels(int prev_page)
{
	if (prev_page >= 0)
	{
		QFont prev_font = m_page_labels[prev_page]->font();
		prev_font.setBold(false);
		m_page_labels[prev_page]->setFont(prev_font);
	}

	const int page = m_ui.pages->currentIndex();
	QFont font = m_page_labels[page]->font();
	font.setBold(true);
	m_page_labels[page]->setFont(font);
}

void SetupWizardDialog::updatePageButtons()
{
	const int page = m_ui.pages->currentIndex();
	m_ui.next->setText((page == Page_Complete) ? tr("&Finish") : tr("&Next"));
	m_ui.back->setEnabled(page > 0);
}

void SetupWizardDialog::confirmCancel()
{
	if (QMessageBox::question(this, tr("Cancel Setup"),
			tr("Are you sure you want to cancel PCSX2 setup?\n\nAny changes have been saved, and the wizard will run "
			   "again next time you start PCSX2.")) != QMessageBox::Yes)
	{
		return;
	}

	reject();
}

void SetupWizardDialog::setupUi()
{
	m_ui.setupUi(this);

	m_ui.logo->setPixmap(QPixmap(QStringLiteral("%1/icons/AppIconLarge.png").arg(QtHost::GetResourcesBasePath())));

	m_ui.pages->setCurrentIndex(0);

	m_page_labels[Page_Language] = m_ui.labelLanguage;
	m_page_labels[Page_BIOS] = m_ui.labelBIOS;
	m_page_labels[Page_PTR2] = m_ui.labelPTR2;
	m_page_labels[Page_Controller] = m_ui.labelController;
	m_page_labels[Page_Complete] = m_ui.labelComplete;

	connect(m_ui.back, &QPushButton::clicked, this, &SetupWizardDialog::previousPage);
	connect(m_ui.next, &QPushButton::clicked, this, &SetupWizardDialog::nextPage);
	connect(m_ui.cancel, &QPushButton::clicked, this, &SetupWizardDialog::confirmCancel);

	setupLanguagePage();
	setupBIOSPage();
	setupPTR2Page();
	//need a mods page
	//setupGameListPage();
	setupControllerPage();
}

void SetupWizardDialog::setupLanguagePage()
{
	SettingWidgetBinder::BindWidgetToEnumSetting(nullptr, m_ui.theme, "UI", "Theme",
		InterfaceSettingsWidget::THEME_NAMES, InterfaceSettingsWidget::THEME_VALUES, QtHost::GetDefaultThemeName(), "InterfaceSettingsWidget");
	connect(m_ui.theme, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SetupWizardDialog::themeChanged);

	for (const std::pair<QString, QString>& it : QtHost::GetAvailableLanguageList())
		m_ui.language->addItem(it.first, it.second);
	SettingWidgetBinder::BindWidgetToStringSetting(
		nullptr, m_ui.language, "UI", "Language", QtHost::GetDefaultLanguage());
	connect(
		m_ui.language, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SetupWizardDialog::languageChanged);

	//SettingWidgetBinder::BindWidgetToBoolSetting(
	//	nullptr, m_ui.autoUpdateEnabled, "AutoUpdater", "CheckAtStartup", true);
}

void SetupWizardDialog::themeChanged()
{
	// Main window gets recreated at the end here anyway, so it's fine to just yolo it.
	QtHost::UpdateApplicationTheme();
}

void SetupWizardDialog::languageChanged()
{
	// Skip the recreation, since we don't have many dynamic UI elements.
	QtHost::InstallTranslator(this);
	m_ui.retranslateUi(this);
}

void SetupWizardDialog::setupBIOSPage()
{
	SettingWidgetBinder::BindWidgetToFolderSetting(nullptr, m_ui.biosSearchDirectory, m_ui.browseBiosSearchDirectory,
		m_ui.openBiosSearchDirectory, m_ui.resetBiosSearchDirectory, "Folders", "Bios",
		Path::Combine(EmuFolders::DataRoot, "bios"));

	refreshBiosList();

	connect(m_ui.biosSearchDirectory, &QLineEdit::textChanged, this, &SetupWizardDialog::refreshBiosList);
	connect(m_ui.refreshBiosList, &QPushButton::clicked, this, &SetupWizardDialog::refreshBiosList);
	connect(m_ui.biosList, &QTreeWidget::currentItemChanged, this, &SetupWizardDialog::biosListItemChanged);
}

void SetupWizardDialog::refreshBiosList()
{
	BIOSSettingsWidget::populateList(m_ui.biosList, m_ui.biosSearchDirectory->text().toStdString());
}

void SetupWizardDialog::biosListItemChanged(const QTreeWidgetItem* current, const QTreeWidgetItem* previous)
{
	Host::SetBaseStringSettingValue("Filenames", "BIOS", current->text(0).toUtf8().constData());
	Host::CommitBaseSettingChanges();
	g_emu_thread->applySettings();
}

void SetupWizardDialog::setupPTR2Page()
{
	SettingWidgetBinder::BindWidgetToFolderSetting(nullptr, m_ui.ptr2Directory, m_ui.browsePtr2Directory,
		nullptr, m_ui.ptr2ResetDirectory, "Folders", "PTR2",
		Path::Combine(EmuFolders::DataRoot, "ptr2"));
	connect(m_ui.ExtractFiles, &QPushButton::clicked, this, &SetupWizardDialog::extractPTR2Files);

	QLineEdit* isoDirectory = m_ui.isoDirectory;

	QObject::connect(m_ui.browseIsoDirectory, &QAbstractButton::clicked, this, [isoDirectory]() {
		QString path(isoDirectory->text());
		if (path.isEmpty())
			path = QString::fromStdString(EmuFolders::DataRoot);
		const QString isoPath(QDir::toNativeSeparators(QFileDialog::getOpenFileName(QtUtils::GetRootWidget(isoDirectory),
			qApp->translate("SettingWidgetBinder", "Select PaRappa The Rapper 2 ISO file"),
			path,
			QString::fromStdString("ISO Image (*.iso);;All Files (*)"))));
		if (isoPath.isEmpty())
			return;
		//QMessageBox::critical(QtUtils::GetRootWidget(isoDirectory), tr("Debug"),
		//	tr(isoPath.toStdString().c_str())
		//	);
		isoDirectory->setText(isoPath);
	});
}
//md5/hash checking stuff is unfinished (low priority
struct file_entry
{
	std::string path;
	//std::string md5;
};

//read a single db file chunk (path, md5) and return

bool ReadOneFile(FILE* stream, file_entry &entry)
{
	long off = ftell(stream);

	char buf[900];
	if (fgets(buf, sizeof(buf), stream) == nullptr)
		return false;
	entry.path = buf;

	//fgets puts file offset at end for some reason, so reset it:
	off += entry.path.length();
	std::fseek(stream, off, SEEK_SET);
	if (entry.path.back() == '\n')
		entry.path = entry.path.substr(0, entry.path.length() - 2);

	/* char buf2[900];
		if (fgets(buf2, sizeof(buf2), stream) == nullptr)
			return false;
	entry.md5 = buf2;

	std::fseek(stream, off + entry.md5.length() + 1, SEEK_SET);
	*/
	return true;
}

bool SetupWizardDialog::extractFileFromISO(IsoReader& isor, const std::string file_iso_path, const char* dest_path, bool& overwrite_set, bool& overwrite)
{
	if (isor.Open())
	{
		if (!isor.FileExists(file_iso_path)){
			DisplayErrorMessage("Could not find file in ISO. (Corrupt/wrong ISO?)", file_iso_path);
			return false;
		}

		std::vector<u8> data;
		if (!isor.ReadFile(file_iso_path, &data)) {
			DisplayErrorMessage("Could not read file from ISO. (Corrupt/wrong ISO?)", file_iso_path);
			return false;
		}

		if (!FileSystem::PathExists(dest_path)) //if file doesnt exist
		{
			if (!FileSystem::WriteBinaryFile(dest_path, data.data(), data.size())) {
				DisplayErrorMessage("Could not write file to extract directory. (Check space/permissions?)", dest_path);
				return false;
			}
		}
		else if (!overwrite_set) //if file exists but user hasn't said whether to always overwrite or not
		{
			if (askOverwrite(std::string(dest_path), overwrite_set, overwrite)) //if user said yes to overwrite
			{
				if (!FileSystem::DeletePath(dest_path)) {
					DisplayErrorMessage("Could not overwrite existing file in extract directory. (Check permissions?)", dest_path);
					return false;
				}
				if (!FileSystem::WriteBinaryFile(dest_path, data.data(), data.size()))
				{
					DisplayErrorMessage("Could not write file to extract directory. (Check space/permissions?)", dest_path);
					return false;
				}
			}
			//if user said no dont do anything
		}
		else if (overwrite) //if file exists and user said to overwrite all
		{
			if (!FileSystem::DeletePath(dest_path))
			{
				DisplayErrorMessage("Could not overwrite existing file in extract directory. (Check permissions?)", dest_path);
				return false;
			}
			if (!FileSystem::WriteBinaryFile(dest_path, data.data(), data.size()))
			{
				DisplayErrorMessage("Could not write file to extract directory. (Check space/permissions?)", dest_path);
				return false;
			}
		}
		return true;
	}
}
void SetupWizardDialog::DisplayErrorMessage(std::string error, std::string path)
{
	QMessageBox msgBox;
	msgBox.setWindowTitle("Error");
	msgBox.setWindowIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical));
	msgBox.setIcon(QMessageBox::Critical);
	msgBox.setText(QString::fromStdString(error));
	if (path != "")
		msgBox.setDetailedText(QString::fromStdString("PATH: " + path));
	msgBox.setStandardButtons(QMessageBox::Ok);
	int ret = msgBox.exec();
}
	//todo:
//iso validation. via CRC maybe?
//general cleanup

//Why did I hardcode it to find files via a db file? instead of just find all files in the iso??
//Because It was going to validate each file's via a hash, but I can't be bothered to add the hash calculation yet
void SetupWizardDialog::extractPTR2Files()
{
	//DisplayErrorMessage("Could not write file to extract directory. (Check space/permissions?)", "C:\\Users\\Owner\\Owner\\yeah\\true\\real moding game\\moderfile.int");			
	bool overwrite_set = false;
	bool overwrite = false;
	std::string iso_path = m_ui.isoDirectory->text().toStdString();
	if (iso_path == "") {
		QMessageBox::information(QtUtils::GetRootWidget(m_ui.isoDirectory), tr("Error"), "Please select a PaRappa 2 ISO.");
		return;
	}
	if (!FileSystem::FileExists(iso_path.c_str())) {
		DisplayErrorMessage("Invalid input ISO path.", iso_path);
		return;
	}
	//setup cdvd to read iso
	CDVDsys_ClearFiles();
	CDVDsys_SetFile(CDVD_SourceType::Iso, iso_path);
	CDVDsys_ChangeSource(CDVD_SourceType::Iso);
	Error error;
	if (!DoCDVDopen(&error)){
		DisplayErrorMessage("Could not open input ISO file. (Check permissions?)", iso_path);
		return;
	}
	IsoReader isor;
	//Hardcode as this is const
	const int iso_filedb_count = 146;

	std::string extract_path = m_ui.ptr2Directory->text().toStdString();

	//open db file
	const std::string ptr2filedb_filename = Path::Combine(EmuFolders::Resources, "iso_extract_db.txt");
	auto fp = FileSystem::OpenManagedCFile(ptr2filedb_filename.c_str(), "rb+");
	if (!fp) {
		DisplayErrorMessage("Could not open iso extract database resource. (Check permissions?)", ptr2filedb_filename);
		return;
	}
	auto stream = fp.get();
	double progress_increment = 100 / iso_filedb_count;
	m_ui.progressBar->setValue(0);
	for (int i = 0; i < iso_filedb_count; i++)
	{
		file_entry entry;
		if (!ReadOneFile(stream, entry)) {
			DisplayErrorMessage("Could not read iso extract database resource. (Check permissions?)", ptr2filedb_filename);
			return;
		}
		
		std::string dest_path = Path::Combine(extract_path, entry.path);
		std::string dest_dir = std::string(Path::GetDirectory(dest_path));
		if (!FileSystem::EnsureDirectoryExists(dest_dir.c_str(), true)) {
			DisplayErrorMessage("Could not create folder in extract directory. (Check permissions?)", dest_dir);
			return;
		}

		if (!extractFileFromISO(isor, entry.path, dest_path.c_str(), overwrite_set, overwrite))
			return;

		//extract int archives
		if (StringUtil::compareNoCase(Path::GetExtension(entry.path.c_str()), "INT"))
		{
			if (!extractINTArchive(dest_path.c_str(), overwrite_set, overwrite))
				return;
		}
		m_ui.progressBar->setValue(progress_increment * (i + 1));
	}
	m_ui.progressBar->setValue(100);
}

/*
void SetupWizardDialog::setupGameListPage()
{
	m_ui.searchDirectoryList->setSelectionMode(QAbstractItemView::SingleSelection);
	m_ui.searchDirectoryList->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_ui.searchDirectoryList->setAlternatingRowColors(true);
	m_ui.searchDirectoryList->setShowGrid(false);
	m_ui.searchDirectoryList->horizontalHeader()->setHighlightSections(false);
	m_ui.searchDirectoryList->verticalHeader()->hide();
	m_ui.searchDirectoryList->setCurrentIndex({});
	m_ui.searchDirectoryList->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);

	connect(m_ui.searchDirectoryList, &QTableWidget::customContextMenuRequested, this, &SetupWizardDialog::onDirectoryListContextMenuRequested);
	connect(m_ui.searchDirectoryList, &QTableWidget::itemSelectionChanged, this, &SetupWizardDialog::onDirectoryListSelectionChanged);
	connect(m_ui.addSearchDirectoryButton, &QPushButton::clicked, this, &SetupWizardDialog::onAddSearchDirectoryButtonClicked);
	connect(m_ui.removeSearchDirectoryButton, &QPushButton::clicked, this, &SetupWizardDialog::onRemoveSearchDirectoryButtonClicked);

	refreshDirectoryList();
}



void SetupWizardDialog::onDirectoryListContextMenuRequested(const QPoint& point)
{
	QModelIndexList selection = m_ui.searchDirectoryList->selectionModel()->selectedIndexes();
	if (selection.size() < 1)
		return;

	const int row = selection[0].row();

	QMenu menu;
	menu.addAction(tr("Remove"), [this]() { onRemoveSearchDirectoryButtonClicked(); });
	menu.addSeparator();
	menu.addAction(tr("Open Directory..."),
		[this, row]() { QtUtils::OpenURL(this, QUrl::fromLocalFile(m_ui.searchDirectoryList->item(row, 0)->text())); });
	menu.exec(m_ui.searchDirectoryList->mapToGlobal(point));
}

void SetupWizardDialog::onDirectoryListSelectionChanged()
{
	m_ui.removeSearchDirectoryButton->setEnabled(!m_ui.searchDirectoryList->selectedItems().isEmpty());
}

void SetupWizardDialog::onAddSearchDirectoryButtonClicked()
{
	QString dir = QDir::toNativeSeparators(QFileDialog::getExistingDirectory(this, tr("Select Search Directory")));

	if (dir.isEmpty())
		return;

	QMessageBox::StandardButton selection = QMessageBox::question(this, tr("Scan Recursively?"),
		tr("Would you like to scan the directory \"%1\" recursively?\n\nScanning recursively takes "
		   "more time, but will identify files in subdirectories.")
			.arg(dir),
		QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
	if (selection == QMessageBox::Cancel)
		return;

	const bool recursive = (selection == QMessageBox::Yes);
	const std::string spath = dir.toStdString();
	Host::RemoveBaseValueFromStringList("GameList", recursive ? "Paths" : "RecursivePaths", spath.c_str());
	Host::AddBaseValueToStringList("GameList", recursive ? "RecursivePaths" : "Paths", spath.c_str());
	Host::CommitBaseSettingChanges();
	refreshDirectoryList();
}

void SetupWizardDialog::onRemoveSearchDirectoryButtonClicked()
{
	const int row = m_ui.searchDirectoryList->currentRow();
	std::unique_ptr<QTableWidgetItem> item((row >= 0) ? m_ui.searchDirectoryList->takeItem(row, 0) : nullptr);
	if (!item)
		return;

	const std::string spath = item->text().toStdString();
	if (!Host::RemoveBaseValueFromStringList("GameList", "Paths", spath.c_str()) &&
		!Host::RemoveBaseValueFromStringList("GameList", "RecursivePaths", spath.c_str()))
	{
		return;
	}

	Host::CommitBaseSettingChanges();
	refreshDirectoryList();
}

void SetupWizardDialog::addPathToTable(const std::string& path, bool recursive)
{
	const int row = m_ui.searchDirectoryList->rowCount();
	m_ui.searchDirectoryList->insertRow(row);

	QTableWidgetItem* item = new QTableWidgetItem();
	item->setText(QString::fromStdString(path));
	item->setFlags(item->flags() & ~(Qt::ItemIsEditable));
	m_ui.searchDirectoryList->setItem(row, 0, item);

	QCheckBox* cb = new QCheckBox(m_ui.searchDirectoryList);
	m_ui.searchDirectoryList->setCellWidget(row, 1, cb);
	cb->setChecked(recursive);

	connect(cb, &QCheckBox::checkStateChanged, this, [item](Qt::CheckState state) {
		const std::string path(item->text().toStdString());
		if (state == Qt::Checked)
		{
			Host::RemoveBaseValueFromStringList("GameList", "Paths", path.c_str());
			Host::AddBaseValueToStringList("GameList", "RecursivePaths", path.c_str());
		}
		else
		{
			Host::RemoveBaseValueFromStringList("GameList", "RecursivePaths", path.c_str());
			Host::AddBaseValueToStringList("GameList", "Paths", path.c_str());
		}
		Host::CommitBaseSettingChanges();
	});
}

void SetupWizardDialog::refreshDirectoryList()
{
	QSignalBlocker sb(m_ui.searchDirectoryList);
	while (m_ui.searchDirectoryList->rowCount() > 0)
		m_ui.searchDirectoryList->removeRow(0);

	std::vector<std::string> path_list = Host::GetBaseStringListSetting("GameList", "Paths");
	for (const std::string& entry : path_list)
		addPathToTable(entry, false);

	path_list = Host::GetBaseStringListSetting("GameList", "RecursivePaths");
	for (const std::string& entry : path_list)
		addPathToTable(entry, true);

	m_ui.searchDirectoryList->sortByColumn(0, Qt::AscendingOrder);
	m_ui.removeSearchDirectoryButton->setEnabled(false);
}

void SetupWizardDialog::resizeDirectoryListColumns()
{
	QtUtils::ResizeColumnsForTableView(m_ui.searchDirectoryList, {-1, 100});
}
*/
void SetupWizardDialog::setupControllerPage()
{
	static constexpr u32 NUM_PADS = 2;

	struct PadWidgets
	{
		QComboBox* type_combo;
		QLabel* mapping_result;
		QToolButton* mapping_button;
	};
	const PadWidgets pad_widgets[NUM_PADS] = {
		{m_ui.controller1Type, m_ui.controller1Mapping, m_ui.controller1AutomaticMapping},
		{m_ui.controller2Type, m_ui.controller2Mapping, m_ui.controller2AutomaticMapping},
	};

	for (u32 port = 0; port < NUM_PADS; port++)
	{
		const std::string section = fmt::format("Pad{}", port + 1);
		const PadWidgets& w = pad_widgets[port];

		for (const auto& [name, display_name] : Pad::GetControllerTypeNames())
			w.type_combo->addItem(QString::fromUtf8(display_name), QString::fromUtf8(name));
		ControllerSettingWidgetBinder::BindWidgetToInputProfileString(
			nullptr, w.type_combo, section, "Type", Pad::GetControllerInfo(Pad::GetDefaultPadType(port))->name);

		w.mapping_result->setText((port == 0) ? tr("Default (Keyboard)") : tr("Default (None)"));

		connect(w.mapping_button, &QAbstractButton::clicked, this,
			[this, port, label = w.mapping_result]() { openAutomaticMappingMenu(port, label); });
	}

	// Trigger enumeration to populate the device list.
	connect(g_emu_thread, &EmuThread::onInputDevicesEnumerated, this, &SetupWizardDialog::onInputDevicesEnumerated);
	connect(g_emu_thread, &EmuThread::onInputDeviceConnected, this, &SetupWizardDialog::onInputDeviceConnected);
	connect(g_emu_thread, &EmuThread::onInputDeviceDisconnected, this, &SetupWizardDialog::onInputDeviceDisconnected);
	g_emu_thread->enumerateInputDevices();
}

void SetupWizardDialog::openAutomaticMappingMenu(u32 port, QLabel* update_label)
{
	QMenu menu(this);
	bool added = false;

	for (const QPair<QString, QString>& dev : m_device_list)
	{
		// we set it as data, because the device list could get invalidated while the menu is up
		QAction* action = menu.addAction(QStringLiteral("%1 (%2)").arg(dev.first).arg(dev.second));
		action->setData(dev.first);
		connect(action, &QAction::triggered, this, [this, port, update_label, action]() {
			doDeviceAutomaticBinding(port, update_label, action->data().toString());
		});
		added = true;
	}

	if (!added)
	{
		QAction* action = menu.addAction(tr("No devices available"));
		action->setEnabled(false);
	}

	menu.exec(QCursor::pos());
}

void SetupWizardDialog::doDeviceAutomaticBinding(u32 port, QLabel* update_label, const QString& device)
{
	std::vector<std::pair<GenericInputBinding, std::string>> mapping =
		InputManager::GetGenericBindingMapping(device.toStdString());
	if (mapping.empty())
	{
		QMessageBox::critical(this, tr("Automatic Binding"),
			tr("No generic bindings were generated for device '%1'. The controller/source may not support automatic "
			   "mapping.")
				.arg(device));
		return;
	}

	bool result;
	{
		auto lock = Host::GetSettingsLock();
		result = Pad::MapController(*Host::Internal::GetBaseSettingsLayer(), port, mapping);
	}
	if (!result)
		return;

	Host::CommitBaseSettingChanges();

	update_label->setText(device);
}

void SetupWizardDialog::onInputDevicesEnumerated(const QList<QPair<QString, QString>>& devices)
{
	m_device_list = devices;
}

void SetupWizardDialog::onInputDeviceConnected(const QString& identifier, const QString& device_name)
{
	m_device_list.emplace_back(identifier, device_name);
}

void SetupWizardDialog::onInputDeviceDisconnected(const QString& identifier)
{
	for (auto iter = m_device_list.begin(); iter != m_device_list.end(); ++iter)
	{
		if (iter->first == identifier)
		{
			m_device_list.erase(iter);
			break;
		}
	}
}
