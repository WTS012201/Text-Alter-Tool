﻿#include "../headers/imageframe.h"

ImageFrame::ImageFrame(QWidget* parent, Ui::MainWindow* __ui, Options* options):
  rubberBand{nullptr}, scene{new QGraphicsScene(this)},
  mode{tesseract::RIL_PARA}, selection{nullptr}, ui{__ui}, scalar{1.0},
  scaleFactor{0.1}
{
  initUi(parent);
  setWidgets();
  connections();
  setOptions(options);
}

ImageFrame::~ImageFrame(){
  delete scene;
  delete matrix;

  for(auto& obj : contentMap.keys()){
    delete contentMap[obj];
    delete obj;
  }
  if(ui->content->layout() != nullptr){
    while(ui->content->layout()->count() != 1){
      QLayoutItem* item = ui->content->layout()->takeAt(0);
      delete item->widget();
      delete item;
    }
  }
}

void ImageFrame::setOptions(Options* options){
  setMode(options->getPartialSelection());
}

void ImageFrame::changeZoom(){
  float val = (ui->zoomFactor->text()).toFloat();

  scalar = (val < 0.1) ? 0.1 : val;
  scalar = (scalar > 10.0) ? 10.0 : scalar;
  ui->zoomFactor->setText(QString::number(scalar));
  changeImage();
  for(auto& obj : contentMap.keys()){
    obj->scaleAndPosition(scalar);
  }
}

void ImageFrame::changeImage(){
  matrix->copyTo(display);
  cv::resize(
        display, display,
        cv::Size{},
        scalar, scalar,
        cv::INTER_CUBIC
        );
  QImage img{
    (uchar*)display.data,
        display.cols,
        display.rows,
        (int)display.step,
        QImage::Format_BGR888
  };
  auto imagePixmap = QPixmap::fromImage(img);

  scene->addPixmap(imagePixmap);
  scene->setSceneRect(imagePixmap.rect());
  scene->update();
  this->setScene(scene);

  this->setMinimumSize(imagePixmap.size());
  this->setMaximumSize(imagePixmap.size());
}

void ImageFrame::changeText(){
  if(!selection){
    qDebug() << "No selection";
    return;
  }

  cv::Mat old;
  matrix->copyTo(old);
  undo.push(old);

  selection->fillText();
  matrix->copyTo(display);
  changeImage();
}

void ImageFrame::connections(){
  connect(ui->zoomFactor, &QLineEdit::editingFinished, this, &ImageFrame::changeZoom);
  connect(this, &ImageFrame::rawTextChanged, this, &ImageFrame::setRawText);
  connect(ui->highlightAll, &QPushButton::pressed, this, &ImageFrame::highlightSelection);
  connect(ui->changeButton, &QPushButton::pressed, this, &ImageFrame::changeText);
}

void ImageFrame::setRawText(){
  populateTextObjects();
}

void ImageFrame::highlightSelection(){
  for(auto& obj : contentMap.keys()){
    if(obj->isSelected){
      obj->isChanged = true;
      obj->deselect();
      contentMap[obj]->show();
    }
  }
}


void ImageFrame::setWidgets(){
  ui->zoomFactor->hide();
  ui->zoomLabel->hide();
}

void ImageFrame::initUi(QWidget* parent){
  parent->setContentsMargins(0,0,0,0);

  this->setParent(parent);
  this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  this->hide();
}

void ImageFrame::zoomIn(){
  if(!keysPressed[Qt::SHIFT]){
    return;
  }
  (scalar + scaleFactor > 10.0) ? scalar = 10.0 : scalar += scaleFactor;
  ui->zoomFactor->setText(QString::number(scalar));
  changeImage();
  for(auto& obj : contentMap.keys()){
    obj->scaleAndPosition(scalar);
  }
}

void ImageFrame::zoomOut(){
  if(!keysPressed[Qt::SHIFT]){
    return;
  }
  (scalar - scaleFactor < 0.1) ? scalar = 0.1 : scalar -= scaleFactor;
  ui->zoomFactor->setText(QString::number(scalar));
  changeImage();
  for(auto& obj : contentMap.keys()){
    obj->scaleAndPosition(scalar);
  }
}

void ImageFrame::mousePressEvent(QMouseEvent* event) {
  if(!keysPressed[Qt::Key_Control]){
    for(auto& obj : contentMap.keys()){
      obj->deselect();
      if(!obj->isChanged){
        contentMap[obj]->hide();
        obj->hide();
      }
    }
  }
  origin = event->pos();
  if(!rubberBand){
    rubberBand = new QRubberBand{QRubberBand::Rectangle, this};
  }
  rubberBand->setGeometry(QRect(origin, QSize{}));
  rubberBand->show();

  if(event->buttons() & Qt::LeftButton){
    zoomIn();
  }else if(event->buttons() & Qt::RightButton){
    zoomOut();
  }
}

void ImageFrame::mouseMoveEvent(QMouseEvent *event){
    rubberBand->setGeometry(QRect(origin, event->pos()).normalized());
}

void ImageFrame::mouseReleaseEvent(QMouseEvent *event){
  rubberBand->hide();
  auto dest = event->pos();

  QPoint tl, br;
  if(origin.x() < dest.x()){
    tl.setX(origin.x());
    br.setX(dest.x());
  }else{
    tl.setX(dest.x());
    br.setX(origin.x());
  }

  if(origin.y() < dest.y()){
    tl.setY(origin.y());
    br.setY(dest.y());
  }else{
    tl.setY(dest.y());
    br.setY(origin.y());
  }

  QPair<QPoint, QPoint> box{tl, br};
  inSelection(box);
}

void ImageFrame::inSelection(QPair<QPoint, QPoint> boundingBox){
  auto a = boundingBox.first;
  auto b = boundingBox.second;

  for(auto& obj : contentMap.keys()){
    auto tl = obj->topLeft;
    auto br = obj->bottomRight;

    bool xOverlap = !((br.x() < a.x()) || (tl.x() > b.x()));
    bool yOverlap = !((br.y() < a.y()) || (tl.y() > b.y()));

    if(xOverlap && yOverlap){
      obj->selectHighlight();
    }
  }
}

void ImageFrame::setImage(QString imageName){
  filepath = imageName;
  scalar = 1.0;

  QPixmap imagePixmap{imageName};

  scene->addPixmap(imagePixmap);
  scene->setSceneRect(imagePixmap.rect());
  scene->update();
  this->setScene(scene);

  this->setMinimumSize(imagePixmap.size());
  this->setMaximumSize(imagePixmap.size());
  extract();
  populateTextObjects();
}

void ImageFrame::showAll(){
  ui->zoomFactor->show();
  ui->zoomLabel->show();
  this->show();
}

void ImageFrame::extract(){
  try{
    matrix = new cv::Mat{cv::imread(filepath.toStdString(), cv::IMREAD_COLOR)};
  }catch(...){
    qDebug() << "error reading image";
    return;
  }

  if(matrix->empty()){
    qDebug() << "empty matrix";
    return;
  }  

  QFuture<void> future = QtConcurrent::run(
  [&](cv::Mat matrix) mutable -> void{
      rawText = collect(matrix);
  }, *matrix).then([&](){
    matrix->copyTo(display);
    emit rawTextChanged();
  });
  showAll();
}

QString ImageFrame::collect(
    cv::Mat& matrix
    ){
  tesseract::TessBaseAPI *api = new tesseract::TessBaseAPI();

  api->Init(nullptr, "eng", tesseract::OEM_DEFAULT);
  api->SetPageSegMode(tesseract::PSM_AUTO);
  api->SetImage(matrix.data, matrix.cols, matrix.rows, 3, matrix.step);
  api->Recognize(0);

  QString text = QString{api->GetUTF8Text()};
  tesseract::ResultIterator* ri = api->GetIterator();

  typedef QPair<QPoint, QPoint> Space;
  QVector<QPair<QString, Space*>> partials;
  int x1, y1, x2, y2;

  bool a1, a2, a3, a4, a5, a6;
  int a7, a8;
  ri->WordFontAttributes(&a1,&a2,&a3,&a4,&a5,&a6,&a7,&a8);

  QFont font = ui->textEdit->font();
  font.setPointSize(a7);
  ui->textEdit->setFont(font);
  ui->fontSizeInput->setText(QString::number(a7));

  if (ri != 0) {
    do {
      QString word = ri->GetUTF8Text(mode);
      ri->BoundingBox(mode, &x1, &y1, &x2, &y2);
      QPoint p1{x1, y1}, p2{x2, y2};

      while(word.endsWith('\n') || word.endsWith(' ')){
        word = word.remove(word.length() - 1, word.length());
      }
      if(word.isEmpty()){
        continue;
      }

      partials.push_back(QPair<QString, Space*>{word, new Space{p1, p2}});
    } while (ri->Next(mode));
  }

  for(auto& partial : partials){
    ImageTextObject* textObject = new ImageTextObject{nullptr};
    textObject->setText(partial.first);
    textObject->addLineSpace(partial.second);
//    textObjects.push_back(textObject);
    contentMap[textObject];
  }

  api->End();
  delete api;
  return text;
}

void ImageFrame::populateTextObjects(){
  QHash<ImageTextObject*, Content*> tempMap;
  auto layout = qobject_cast<QVBoxLayout*>(ui->content->layout());

  for(auto key : contentMap.keys()){
    ImageTextObject* temp = new ImageTextObject{this, *key, ui, matrix};
    temp->hide();

    Content* content = new Content{};
    content->setTextObject(temp);

    auto contentLabel = content->getLabel();
    contentLabel->setText(temp->getText());
    contentLabel->setStyleSheet("border: 1px solid black");
    layout->insertWidget(layout->count() - 1, content);
    content->hide();

    tempMap[temp] = content;

    connect(temp, &ImageTextObject::selection, this, [&](){
      selection = qobject_cast<ImageTextObject*>(sender());
      for(auto& obj : contentMap.keys()){
        if(obj == selection){
          continue;
        }
        if(!obj->isChanged){
          contentMap[obj]->hide();
        }
        obj->deselect();
      }
    });

    delete key;
  }

  contentMap = tempMap;
}

void ImageFrame::setMode(tesseract::PageIteratorLevel __mode){
  mode = __mode;
}

void ImageFrame::undoAction(){
  if(undo.empty()){
    return;
  }

  auto old = undo.pop();
  redo.push(*matrix);
  *matrix = old;
  changeImage();
}

void ImageFrame::redoAction(){
  if(redo.empty()){
    return;
  }

  auto old = redo.pop();
  undo.push(*matrix);
  *matrix = old;
  changeImage();
}
