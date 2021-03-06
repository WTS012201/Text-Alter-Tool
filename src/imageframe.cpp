
#include "../headers/imageframe.h"

ImageFrame::ImageFrame(QWidget* parent, Ui::MainWindow* __ui, Options* options):
  rubberBand{nullptr}, scene{new QGraphicsScene(this)},
  mode{tesseract::RIL_PARA}, selection{nullptr}, ui{__ui}, state{new State},
  scalar{1.0}, scaleFactor{0.1}, middleDown{false}
{
  initUi(parent);
  setWidgets();
  connections();
  setOptions(options);
}

ImageFrame::~ImageFrame(){
  for(const auto& state : undo){
    delete state;
  }
  for(const auto& state : redo){
    delete state;
  }
  for(const auto& obj : state->textObjects){
    delete obj;
  }
  delete state;
  delete scene;
}

cv::Mat ImageFrame::getImageMatrix(){
  return state->matrix;
}

void ImageFrame::setMode(tesseract::PageIteratorLevel __mode){
  mode = __mode;
}

void ImageFrame::setOptions(Options* options){
  setMode(options->getPartialSelection());
}

void ImageFrame::changeZoom(){
  double val = (ui->zoomFactor->text()).toDouble();

  scalar = (val < 0.1) ? 0.1 : val;
  scalar = (scalar > 10.0) ? 10.0 : scalar;
  ui->zoomFactor->setText(QString::number(scalar));
  changeImage();
  for(auto& obj : state->textObjects){
    obj->scaleAndPosition(scalar);
  }
}

void ImageFrame::pasteImage(QImage* img){
  scalar = 1.0;

  auto imagePixmap = QPixmap::fromImage(*img);

  scene->addPixmap(imagePixmap);
  scene->setSceneRect(imagePixmap.rect());
  scene->update();
  this->setScene(scene);

  this->setMinimumSize(imagePixmap.size());
  this->setMaximumSize(imagePixmap.size());

  QImage cpy = *img;
  cpy.convertTo(QImage::Format_BGR888);
  cv::Mat mat{
        cpy.height(),
        cpy.width(),
        CV_8UC3,
        cpy.bits()
  };

//  convert to cv mat and pass to extract
  extract(&mat);
  populateTextObjects();
}

void ImageFrame::changeImage(QImage* img){
  state->matrix.copyTo(display);
  cv::resize(
        display, display,
        cv::Size{},
        scalar, scalar,
        cv::INTER_CUBIC
        );

  if(!img){
    img = new QImage{
        (uchar*)display.data,
        display.cols,
        display.rows,
        (int)display.step,
        QImage::Format_BGR888
    };
  }

  auto imagePixmap = QPixmap::fromImage(*img);

  scene->addPixmap(imagePixmap);
  scene->setSceneRect(imagePixmap.rect());
  scene->update();
  this->setScene(scene);

  this->setMinimumSize(imagePixmap.size());
  this->setMaximumSize(imagePixmap.size());
  delete img;
}

void ImageFrame::changeText(){
  if(!selection){
    qDebug() << "No selection";
    return;
  }
  const cv::Scalar colorSelection = selection->fontIntensity;
  QColor color{
    (int)selection->fontIntensity[2],
    (int)selection->fontIntensity[1],
    (int)selection->fontIntensity[0],
  };
  QVector<ImageTextObject*> oldObjs = state->textObjects;
  state->textObjects.remove(state->textObjects.indexOf(selection));
  selection->hide();
  selection->setDisabled(true);
  selection = new ImageTextObject{this, *selection, ui, &state->matrix};
  state->textObjects.push_back(selection);

  State* oldState = new State{
      oldObjs,
      cv::Mat{}
  };
  state->matrix.copyTo(oldState->matrix);
  undo.push(oldState);

  selection->fillBackground();
  state->matrix.copyTo(display);
  QImage* img = new QImage{
      (uchar*)display.data,
      display.cols,
      display.rows,
      (int)display.step,
      QImage::Format_BGR888
  };
  QPainter p;
  if(!p.begin(img)){
    qDebug() << "error with painter";
    return;
  }
  int fontSize = ui->fontSizeInput->text().toInt();
  QString label = ui->textEdit->toPlainText();
  selection->setText(label);
  QFont font{"Times", fontSize};
  p.setFont(font);

  QFontMetrics fm{p.font()};
  double x = fm.horizontalAdvance(label);
  double y = fm.height();

  QPoint wh{selection->topLeft.x() + (int)x, selection->topLeft.y() + (int)y};
  QRect oldRect{selection->topLeft, selection->bottomRight};

  auto offset = wh.y() - selection->bottomRight.y();
  selection->topLeft.setY(
        selection->topLeft.y() - offset
        );
  selection->bottomRight = wh;
  selection->bottomRight.setY(selection->bottomRight.y() - offset);
  QRect rect{selection->topLeft, selection->bottomRight};

  double newWidth = rect.width()*1.0/oldRect.width();
  double newHeight = rect.height()*1.0/oldRect.height();
  selection->scaleAndPosition(newWidth, newHeight);

  p.save();
  p.setPen(color);
  p.drawText(rect, label, Qt::AlignCenter | Qt::AlignLeft);
  p.restore();
  p.end();

  // update state for changes
  state->matrix = cv::Mat{
        img->height(), img->width(),
        CV_8UC3, (void*)img->constBits(),
        (size_t)img->bytesPerLine()
  };
  selection->isChanged = true;
  selection->showHighlights();
  selection->mat = &state->matrix;
  selection->fontIntensity = colorSelection;
  delete img;

  changeImage();
}

void ImageFrame::connections(){
  connect(ui->zoomFactor, &QLineEdit::editingFinished, this, &ImageFrame::changeZoom);
  connect(this, &ImageFrame::rawTextChanged, this, &ImageFrame::setRawText);
  connect(ui->highlightAll, &QPushButton::pressed, this, &ImageFrame::highlightSelection);
  connect(ui->changeButton, &QPushButton::pressed, this, &ImageFrame::changeText);

  connect(ui->removeSelection, &QPushButton::pressed, this, [=](...){
    for(auto& obj : state->textObjects){
      if(obj->isSelected){
        obj->deselect();
        obj->hide();
        obj->isChanged = false;
      }
    }
  });
}

void ImageFrame::setRawText(){
  populateTextObjects();
}

void ImageFrame::highlightSelection(){
  for(auto& obj : state->textObjects){
    if(obj->isSelected){
      obj->isChanged = true;
      obj->deselect();
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
  (scalar + scaleFactor > 10.0) ? scalar = 10.0 : scalar += scaleFactor;
  ui->zoomFactor->setText(QString::number(scalar));
  changeImage();
  for(auto& obj : state->textObjects){
    obj->scaleAndPosition(scalar);
  }
}

void ImageFrame::zoomOut(){
  (scalar - scaleFactor < 0.1) ? scalar = 0.1 : scalar -= scaleFactor;
  ui->zoomFactor->setText(QString::number(scalar));
  changeImage();
  for(auto& obj : state->textObjects){
    obj->scaleAndPosition(scalar);
  }
}

void ImageFrame::mousePressEvent(QMouseEvent* event) {
  if(!keysPressed[Qt::Key_Control]){
    for(auto& obj : state->textObjects){
      obj->deselect();
      if(!obj->isChanged){
        obj->hide();
      }
    }
  }
  origin = event->pos();
  if(!rubberBand){
    rubberBand = new QRubberBand{QRubberBand::Rectangle, this};
  }
  rubberBand->setGeometry(QRect{origin, QSize{}});
  rubberBand->show();
}

void ImageFrame::wheelEvent(QWheelEvent* event){
  if(event->angleDelta().y() > 0 && (event->buttons() & Qt::MiddleButton))
    zoomIn();
  else if(event->angleDelta().y() < 0 && (event->buttons() & Qt::MiddleButton))
    zoomOut();
  else{
    QGraphicsView::wheelEvent(event);
  }
}

void ImageFrame::mouseMoveEvent(QMouseEvent *event){
    rubberBand->setGeometry(QRect{origin, event->pos()}.normalized());
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

  QPair<QPoint, QPoint> box{tl / scalar, br / scalar};
  inSelection(box);
}

void ImageFrame::inSelection(QPair<QPoint, QPoint> boundingBox){
  auto a = boundingBox.first;
  auto b = boundingBox.second;

  for(auto& obj : state->textObjects){
    if(!obj->isEnabled()){
      continue;
    }
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

void ImageFrame::extract(cv::Mat* mat){
  if(mat){
    mat->copyTo(state->matrix);
  } else{
    try{
      state->matrix = cv::Mat{
          cv::imread(filepath.toStdString(), cv::IMREAD_COLOR)
      };
    }catch(...){
      qDebug() << "error reading image";
      return;
    }
  }

  if(state->matrix.empty()){
    qDebug() << "empty matrix";
    return;
  }

  QFuture<void> future = QtConcurrent::run(
  [&](cv::Mat matrix) mutable -> void{
      rawText = collect(matrix);
  }, state->matrix).then([&](){
    state->matrix.copyTo(display);
    emit rawTextChanged();
  });
  showAll();
}

void ImageFrame::populateTextObjects(){
  QVector<ImageTextObject*> tempObjects;

  for(auto obj : state->textObjects){
    ImageTextObject* temp = new ImageTextObject{
        this, *obj, ui, &state->matrix
    };
    temp->hide();

    tempObjects.push_back(temp);

    connect(temp, &ImageTextObject::selection, this, [&](...){
      selection = qobject_cast<ImageTextObject*>(sender());
      for(auto& tempObj : state->textObjects){
        if(tempObj == selection){
          continue;
        }
        tempObj->deselect();
      }
    });

    delete obj;
  }

  state->textObjects = tempObjects;
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

  int x1, y1, x2, y2;

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

      ImageTextObject* textObject = new ImageTextObject{nullptr};
      textObject->setText(word);
      textObject->lineSpace = QPair<QPoint, QPoint>{p1, p2};
      textObject->topLeft = p1;
      textObject->bottomRight = p2;
      state->textObjects.push_back(textObject);
    } while (ri->Next(mode));
  }

  api->End();
  delete api;
  return text;
}

void ImageFrame::undoAction(){
  if(undo.empty()){
    return;
  }

  for(auto& obj : state->textObjects){
    obj->hide();
    obj->setDisabled(true);
  }
  State* currState = new State{
      state->textObjects,
      cv::Mat{}
  };
  state->matrix.copyTo(currState->matrix);

  redo.push(currState);
  state = undo.pop();

  for(auto& obj : state->textObjects){
    obj->scaleAndPosition(ui->zoomFactor->text().toDouble());
    obj->show();
    obj->setDisabled(false);
  }

  selection = state->textObjects.last();
  selection->showHighlights();
  changeImage();
}

void ImageFrame::redoAction(){
  if(redo.empty()){
    return;
  }

  for(auto& obj : state->textObjects){
    obj->hide();
    obj->setDisabled(true);
  }

  undo.push(state);
  state = redo.pop();

  for(auto& obj : state->textObjects){
    obj->scaleAndPosition(ui->zoomFactor->text().toDouble());
    obj->show();
    obj->setDisabled(false);
  }

  selection = state->textObjects.last();
  selection->showHighlights();
  changeImage();
}
