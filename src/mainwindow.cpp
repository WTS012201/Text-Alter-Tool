#include "../headers/mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent), iFrame{nullptr}
  , ui(new Ui::MainWindow)
{
  loadData();
  initUi();
  connections();

  iFrame = new ImageFrame(ui->scrollAreaWidgetContents, ui, options);
  ui->scrollHorizontalLayout->addWidget(iFrame);
//  iFrame->setImage("/home/will/screenshots/use_this.png");
}

MainWindow::~MainWindow()
{
  delete ui;
  delete undo;
  delete redo;
}

void MainWindow::initUi(){
  ui->setupUi(this);
  options = new Options{this};
  colorMenu = new ColorTray{this};
}

void MainWindow::loadData(){
  QFileInfo check_file("eng.traineddata");

  if (check_file.exists() && check_file.isFile()) {
    return;
  }
  QFile file{"eng.traineddata"}, qrcFile(":/other/eng.traineddata");
  if(!qrcFile.open(QFile::ReadOnly | QFile::Text)){
    qDebug() << "failed to open qrc file";
  }
  if(!file.open(QFile::WriteOnly | QFile::Text)){
    qDebug() << "failed to write to file";
  }
  file.write(qrcFile.readAll());
}

void MainWindow::on_actionOptions_triggered()
{
//  options->setView(0);
  options->setModal(true);
  if(options->exec() == QDialog::DialogCode::Rejected)
    return;
}

void MainWindow::connections(){
  paste = new QShortcut{QKeySequence("Ctrl+V"), this};
  open = new QShortcut{QKeySequence("Ctrl+O"), this};
  save = new QShortcut{QKeySequence("Ctrl+S"), this};
  undo = new QShortcut{QKeySequence("Ctrl+Z"), this};
  redo = new QShortcut{QKeySequence("Ctrl+Shift+Z"), this};
  clipboard = QApplication::clipboard();

  connect(ui->fontBox, SIGNAL(activated(int)), this, SLOT(fontSelected()));
  QObject::connect(
        ui->fontSizeInput,
        &QLineEdit::textChanged,
        this,
        &MainWindow::fontSizeChanged
        );

  QObject::connect(ui->color, &QPushButton::clicked, this, &MainWindow::colorTray);
  QObject::connect(paste, &QShortcut::activated, this, &MainWindow::pastImage);
  QObject::connect(open, &QShortcut::activated, this, [&](){
    on_actionOpen_Image_triggered();
  });
  QObject::connect(save, &QShortcut::activated, this, [&](){
    on_actionSave_Image_triggered();
  });
  QObject::connect(undo, &QShortcut::activated, this, [&](){
    on_actionUndo_triggered();
  });
  QObject::connect(redo, &QShortcut::activated, this, [&](){
    on_actionRedo_2_triggered();
  });
}

void MainWindow::colorTray(){
  colorMenu->setModal(true);
  if(iFrame->selection){
    colorMenu->setColor(iFrame->selection->fontIntensity);
  }
  if(colorMenu->exec() == QDialog::DialogCode::Rejected)
    return;

  cv::Scalar scalar{
    (double)colorMenu->color.blue(),
    (double)colorMenu->color.green(),
    (double)colorMenu->color.red(),
  };
  iFrame->selection->fontIntensity = scalar;
}

void MainWindow::pastImage(){
  const QMimeData *mimeData = clipboard->mimeData();

  if (mimeData->hasImage()){
    QImage img = qvariant_cast<QImage>(mimeData->imageData());
    if(!img.isNull()){
      iFrame->pasteImage(&img);
    }
  }
}

void MainWindow::fontSelected(){
  QString text = ui->fontBox->currentText();
  ui->textEdit->setFont(QFont{text, ui->textEdit->font().pointSize()});
}

void MainWindow::fontSizeChanged(){
  QFont font = ui->textEdit->font();
  font.setPointSize(ui->fontSizeInput->text().toInt());
  ui->textEdit->setFont(font);
}

void MainWindow::on_actionUndo_triggered(){
  if(!iFrame){
    return;
  }
  iFrame->undoAction();
}

void MainWindow::on_actionRedo_2_triggered(){
  if(!iFrame){
    return;
  }
  iFrame->redoAction();
}

void MainWindow::keyPressEvent(QKeyEvent* event){
    iFrame->keysPressed[event->key()] = true;
    if(event->key() & Qt::SHIFT){
      this->setCursor(Qt::CursorShape::PointingHandCursor);
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent* event){
    iFrame->keysPressed[event->key()] = false;
    if(event->key() & Qt::SHIFT){
      this->setCursor(Qt::CursorShape::ArrowCursor);
    }
}

void MainWindow::on_actionOpen_Image_triggered()
{
  QFileDialog dialog(this);
  QStringList selection, filters{"*.png *.jpeg *.jpg"};

  dialog.setNameFilters(filters);
  dialog.setFileMode(QFileDialog::ExistingFile);
  if (!dialog.exec()){
      return;
  }
  selection = dialog.selectedFiles();

  if(iFrame){
    delete iFrame;
  }
  iFrame = new ImageFrame(ui->scrollAreaWidgetContents, ui, options);
  ui->scrollHorizontalLayout->addWidget(iFrame);
  iFrame->setImage(selection.first());
}

void MainWindow::on_actionSave_Image_triggered(){
  auto saveFile = QFileDialog::getSaveFileName(0,"Save file",QDir::currentPath(),".png");
//                                               ".png;;.jpeg;;.jpg");
  cv::Mat image = iFrame->getImageMatrix();
  cv::imwrite(saveFile.toStdString() + ".png", image);
}
