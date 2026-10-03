# 4.1 Otonom Kilitlenme (Master Versiyon)

Bu bölüm, Savaşan İHA workspace analizlerinin sentezidir. Kaynak: `02_Ana_Sistem_CPP` mimarisi, DeepStream/YOLO/NvDCF hattı, Alacakart köprüsü ve kontrol katmanı.

---

## 4.1 Otonom Kilitlenme

Aşağıdaki formüller, üst seviye rehberlik ve kilit politikası için kullanılan **sistemdeki** tanımlarla uyumludur (yaklaşım ve katsayılar yapılandırma ile sahaya göre seçilir).

### Matematiksel çerçeve ve temel formüller

#### (a) Görüntü gözleminden mesafe ve hedef hata vektörü (`WorldTargetEstimator`)

Normalize sınırlayıcı kutu genişlik/yüksekliği \(w_n, h_n \in [0,1]\), normalize merkez \((n_x, n_y)\). Görüntü ofseti (görüntü merkezine göre):

\[
e_x^{\mathrm{img}} = n_x - \tfrac{1}{2}, \qquad e_y^{\mathrm{img}} = n_y - \tfrac{1}{2}.
\]

Alan \(A = w_n h_n\) (alt sınırla kararlı hale getirilir). **Mesafe \(d\)** (metre) iki moddan biriyle kestirilir:

- **Kalibre pinhole benzeri model** (odak \(f\) [piksel], hedef gerçek genişlik \(W\) [m], görüntü genişliği \(W_{\mathrm{img}}\) [piksel]):

\[
w_{\mathrm{px}} = w_n \, W_{\mathrm{img}}, \qquad d = \frac{f \, W}{w_{\mathrm{px}}}.
\]

- **Alan tabanlı pratik model** (kazanç \(k_d\)):

\[
d = \frac{k_d}{\sqrt{A}}.
\]

Mesafe \([d_{\min}, d_{\max}]\) aralığına kırpılır. **Stand-off** \(d^\*\) için ileri eksen (sürüş eksenine bağlı tanımda) hata:

\[
e_{\mathrm{long}} = d - d^\*.
\]

Yanal ve düşey hata bileşenleri (lateral/vertical kazançlar \(k_{\ell}, k_v\)):

\[
e_y = e_x^{\mathrm{img}} \, d \, k_{\ell}, \qquad e_z = -\, e_y^{\mathrm{img}} \, d \, k_v.
\]

Güven için merkez ve alan skorları birleştirilir; örnek ağırlıklı birleşim:

\[
s_{\mathrm{center}} = 1 - \min\!\bigl(|e_x^{\mathrm{img}}| + |e_y^{\mathrm{img}}|,\, 1\bigr), \quad
s_{\mathrm{area}} = \min\!\bigl(A / A_{\mathrm{ref}},\, 1\bigr), \quad
\mathrm{conf} = \tfrac{1}{2} s_{\mathrm{center}} + \tfrac{1}{2} s_{\mathrm{area}}.
\]

#### (b) Görüntü merkezine yakınlık skoru (hibrit kilit seçimi)

Çerçeve boyutu \(W_f, H_f\); bbox merkezi \((c_x, c_y)\). Kare mesafe:

\[
d_{\mathrm{pix}}^2 = (c_x - W_f/2)^2 + (c_y - H_f/2)^2, \quad
D = (W_f/2)^2 + (H_f/2)^2.
\]

**Merkez skoru** (1 = tam merkez):

\[
s_{\mathrm{c}} = 1 - \min\!\bigl(d_{\mathrm{pix}}^2 / D,\, 1\bigr).
\]

#### (c) Kilit sağlığı (hibrit mod; ağırlıklı skor)

Güven \(c \in [0,1]\), yukarıdaki \(s_{\mathrm{c}}\), kaçırma sayacı \(m\) ve \(m_{\max}\) üzerinden istikrar \(s_{\mathrm{stab}} = 1 - \min(m/m_{\max}, 1)\), kimlik tutarlılığı \(s_{\mathrm{id}} \in \{0{,}5,\,1\}\) (kilit var ve track_id eşleşiyorsa 1). Ağırlıklar \(w_{\cdot}\) normalize edilir; **kilit sağlığı**:

\[
H_{\mathrm{lock}} = \mathrm{clamp}\bigl(w_c c + w_{\mathrm{c}} s_{\mathrm{c}} + w_s s_{\mathrm{stab}} + w_i s_{\mathrm{id}},\, 0,\, 1\bigr).
\]

#### (d) Telemetri ve görüntü kaynaklı hız füzyonu (`VehicleStateEstimator`)

Ağırlıklar \(w_t, w_v \ge 0\), normalize \( \tilde w_t = w_t/(w_t+w_v)\), \(\tilde w_v = w_v/(w_t+w_v)\). Her eksen için ölçümler \(v^{(t)}\), \(v^{(v)}\); iki kaynak geçerliyse:

\[
v_{\mathrm{fused}} = \tilde w_t \, v^{(t)} + \tilde w_v \, v^{(v)}.
\]

Yalnız bir kaynak geçerliyse o kaynak doğrudan kullanılır. **Yenilik (innovation)** \(\nu = v_{\mathrm{fused}} - v_{\mathrm{prev}}\) mutlak bir **kapı** \(g\) ile kısıtlanır: \(\nu \leftarrow \mathrm{clamp}(\nu, -g, g)\). **Üstel düzeltme (LPF benzeri)**:

\[
v_{\mathrm{new}} = v_{\mathrm{prev}} + \alpha \, \nu, \quad \alpha \in [0,1].
\]

Konum (entegrasyon), \(\Delta t\) adımında:

\[
\mathbf{p} \leftarrow \mathbf{p} + \mathbf{v}\,\Delta t.
\]

Yaw hızı için aynı yapı (ölçüm çoğunlukla telemetriden) ayrı \(\alpha\) ve kapı ile uygulanır.

#### (e) Üst seviye PID (`PidControllerXYZYaw`)

Ölü bant \(\delta\): \(|e| < \delta \Rightarrow e := 0\). Geriye fark ile türev:

\[
D_k = \frac{e_k - e_{k-1}}{\Delta t}.
\]

İntegral adayı \(I_k' = \mathrm{clamp}(I_{k-1} + e_k \Delta t,\,-I_{\lim},\,I_{\lim})\); çıkış doyumunda integratör **anti-windup** ile güncellenir. Ham çıkış:

\[
u_k^{\mathrm{raw}} = K_p e_k + K_i I_k + K_d D_k, \qquad u_k = \mathrm{clamp}(u_k^{\mathrm{raw}},\,-U_{\lim},\,U_{\lim}).
\]

**Slewrate:** \(|u_k - u_{k-1}| \le \rho \,\Delta t\) ile \(u_k\) tekrar kısıtlanır. Eksenler \(x,y,z,\psi\) için aynı yapı; çıktılar üst seviye **\(v_x, v_y, v_z\)** ve **yaw hızı** komutlarıdır.

#### (f) Çoklu nesne takipçisi (NvDCF) — literatür çerçevesi

Dedektör ve ilişkilendirme katmanında durum genelde **Konum + boyut + hız** bileşenleriyle temsil edilir; proje dokümantasyonunda örnek durum vektörü \(\mathbf{x} = [x_c,\, y_c,\, w,\, h,\, v_x,\, v_y]^{\mathsf T}\). **Kalman benzeri** kestirimde öngörü ve güncelleme:

\[
\hat{\mathbf{x}}_{k|k-1} = \mathbf{F}\,\hat{\mathbf{x}}_{k-1|k-1}, \quad
\mathbf{P}_{k|k-1} = \mathbf{F}\mathbf{P}_{k-1|k-1}\mathbf{F}^{\mathsf T} + \mathbf{Q},
\]
\[
\hat{\mathbf{x}}_{k|k} = \hat{\mathbf{x}}_{k|k-1} + \mathbf{K}_k(\mathbf{z}_k - \mathbf{H}\hat{\mathbf{x}}_{k|k-1}).
\]

NvDCF içinde **IoU** ve **görsel benzerlik** veri ilişkilendirmesinde kullanılır; ayrıntılı kovaryans ve \(\mathbf{F},\mathbf{H}\) matrisleri NVIDIA nvtracker uygulamasında kapalıdır.

---

### 4.1.1 Sunucudan gelen verilerin değerlendirilmesi ve takibe en uygun hedefin seçilme mantığı

Savaşan İHA mimarisinde otonom kilitlenmenin **karar çekirdeği uç birimdedir**: DeepStream hattı üzerinden **YOLO tabanlı nesne çıkarımı** ve **NvDCF çoklu nesne takibi** sonrasında adaylar, **kilit seçim politikası** kapsamında değerlendirilir. Politika yalnızca anlık **tespit güvenine** dayanmaz; **görüntü merkezine yakınlık**, **ROI (ilgi bölgesi) uygunluğu**, **kısa süreli kaçırma toleransı (grace)**, **takip kimliği sürekliliği** ve çok bileşenli **kilit sağlığı** birlikte ele alınır. **Baseline** modda, aktif kilit varken **aynı track kimliğinin korunması** önceliklidir; **hibrit** modda güven ve merkez skorları **ağırlıklı birleştirilir**, ROI ve sağlık metrikleri ile adaylar sıralanır. Kimlik değişimi gerektiğinde **mesafe ve güven tabanlı grace** ile ani sıçramalar sınırlanır. Uygulama tarafında, kilitin **belirli bir minimum süre boyunca tutarlı** kalması gibi **doğrulama eşikleri** (sabit süre parametresi) kilit güvenilirliğini artırmak için tanımlanabilir; bu yaklaşım şartnamenin **tekrarlanabilir ve süreklilik arz eden kilit** beklentisiyle örtüşür.

**Yarışma sunucusu** ile etkileşimde sistem, tipik şartname gereksinimleri doğrultusunda **telemetri ve kilit bilgisinin raporlanması** ile **sunucu zamanına hizalama** işlevlerini üstlenir: hız ve yaw hızı özetleri, geçerliyse **normalize görüntü düzleminde hedef konumu** ve **takip kimliği**, senkronize zaman damgalarıyla iletilir; isteğe bağlı olarak **QR veya görev koordinatları** sunucudan çekilip önbelleğe alınarak görev mantığına beslenebilir. Böylece **kritik kilit kararı ağ gecikmesinden kopuk** kalırken, **ölçülebilirlik ve denetlenebilirlik** jüri önünde gerekçelendirilir.

**Kilit doğrulama süresi:** Kilit kararının güvenilirliğini artırmak için, aday hedefin **en az dört saniyelik bir süre boyunca sürekli ve tutarlı biçimde kilit altında** kalması gibi bir **minimum süre eşiği** uygulanabilir. Bu süre zarfında hedefin görüntü merkezine göre tutarlı konumda kalması, tespit güven skorunun oynak olmaması ve **ROI** sınırları içinde bulunması birlikte değerlendirilir; böylece yalnızca kısa süreli veya rastlantısal tespitlerin “doğrulanmış kilit” olarak kabul edilmesi engellenir ve şartnamedeki **süreklilik** beklentisi güçlenir.

**[Buraya Hedef Seçim ve Kilit Sağlığı Akış Diyagramı Eklenecek]**

### 4.1.2 Seçilen hedefe yaklaşma stratejisi ve izlenecek rotanın (path) algoritmik temeli

Yaklaşma, bu projede klasik **global waypoint planlayıcısı** yerine **görüşe dayalı üst seviye hedefleme** olarak kurgulanır: **normalize görüntü koordinatları** ve **sınırlayıcı kutu** üzerinden hedefe uzaklık, ya **kalibre pinhole** (odak ve hedef gerçek genişliği ile) ya da **kutu alanına dayalı pratik mesafe ölçeği** ile kestirilir; **hedeflenen stand-off mesafesi** ile fark alınarak **ileri eksen hatası**, görüntü merkezine göre ofsetler ise mesafe ile ölçeklenerek **yanal ve düşey hata bileşenlerine** dönüştürülür. “Rota”, önceden çizilmiş bir eğri değil; her kontrol adımında güncellenen **hata vektörünün** izlendiği **kapalı çevrim görüş takibi** olarak modellenir.

Üst seviye rehberlik etkin olduğunda bu hatalar **çok eksenli PID** ile **önerilen gövde hızları ve yaw hızı** komutlarına çevrilir ve **Alacakart seri köprüsü** üzerinden iletilir. **Telemetri ile görüntü kaynaklı hız ipuçları**, ağırlıklı füzyon ve **yenilik sınırlamalı düşük geçiren filtreleme** ile birleştirilerek komutların **jitter ve kısa süreli ölçüm bozulmalarına** karşı kararlılığı artırılır. **Otomatik tracker profil önerisi** (ör. dinamik senaryoda daha agresif, istikrarlı takipte daha sakin profil), kilit kalitesi ve platform sağlığına göre **uyarlanabilirlik** sağlar ve farklı yaklaşım senaryolarına uyumu destekler.

**Kaçış ve dayanıklılık:** Yaklaşma/rehberlik zincirine ek olarak **EvasionController**, **kilit kaybı**, **tehdit yakınlığı** veya **harici tetik sinyalleri** gibi durumlarda platformun **anında kaçış manevrası** komutları üretebilmesini sağlar; bu davranış üst seviye komutların Alacakart otopilot köprüsü üzerinden iletilmesiyle uyumludur ve şartname açısından **güvenlik ile dayanıklılık** boyutunu tamamlar.

**[Buraya Görüş Hatasından Dünya Çerçevesi Hata Vektörüne Dönüşüm Şeması Eklenecek]**  
**[Buraya Yaklaşma / Rehberlik Kapalı Çevrim Blok Diyagramı Eklenecek]**

### 4.1.3 Nesne tespit ve takip algoritmalarının seçimi / geliştirilmesi (alternatifleriyle kıyaslamalı)

**Tespit** katmanında **DeepStream nvinfer** altında **YOLO ailesi** modellerinin **TensorRT** ile çalıştırılması, Jetson **edge GPU** üzerinde **NVMM bellek yolu** ve üretimde olgunlaşmış eklenti zinciri ile **düşük gecikmeli çıkarım** hedeflenmesine hizmet eder. **Takip** katmanında **nvtracker** ile **NvDCF** kullanımı; çoklu hedefde **durum kestirimi**, **IoU tabanlı ilişkilendirme** ve **görsel benzerlik** bileşenlerini birleştirerek kimlik sürekliliğini güçlendirmeyi amaçlar.

| Yaklaşım | Bu projede durum | Avantajlar | Dezavantajlar |
|----------|------------------|------------|---------------|
| **YOLO + TensorRT (nvinfer)** | Kullanılıyor | Yüksek FPS potansiyeli; DeepStream ile doğal entegrasyon | Çözünürlük ve modele duyarlı gecikme; saha yeniden kalibrasyonu |
| **R-CNN ailesi** | Ana hat değil | Yüksek yerellik doğruluğu potansiyeli | Genelde daha ağır; gerçek zamanlı kilit için risk |
| **Klasik HOG/SVM vb.** | Ana hat değil | Hafif, yorumlanabilir | Modern saha koşullarında genelde yetersiz |
| **NvDCF (nvtracker)** | Kullanılıyor | NVIDIA yolunda optimize çoklu hedef takibi | Kapalı uygulama; ince ayar ve sahaya özel tuning |
| **SORT / IoU ağırlıklı** | Ana hat değil | Hızlı ve hafif | Örtülme ve manevrada ID kopması riski |
| **DeepSORT / ByteTrack sınıfı** | Ana hat değil | Literatürde güçlü ID yönetimi çeşitleri | Ek maliyet veya entegrasyon; NvDCF yerine doğrudan tak-çalıştır değil |
| **BoT-SORT / StrongSORT sınıfı** | Ana hat değil | Yüksek doğruluk potansiyeli | Gömülü sistemde hesaplama ve entegrasyon yükü |

**Gerekçe:** Şartnamedeki **sürekli tespit ve izlenebilir takip** için **DeepStream + YOLO + NvDCF** üçlüsü, Jetson üzerinde **ölçülebilir uçtan uca gecikme** ve **mühendislik bakım maliyeti** açısından dengeli birincil hat olarak seçilmiştir.

**[Buraya Tespit → Takip → Kilit Zinciri Basitleştirilmiş Blok Şeması Eklenecek]**

### 4.1.4 Görüntü işleme sisteminden gelen piksel / merkez hatasına göre duruş ve hız kontrolü (alternatifleriyle kıyaslamalı)

Görüntü düzlemindeki **hedef–merkez sapması** ve **kutu ölçeği**, **basit kamera ve ölçek varsayımlarıyla** anlamlı **hedef durum hatasına** dönüştürülür; ardından **çok eksenli PID** ile **doğrusal hız komutları** ve **yaw hızı** üretilir. PID tarafında **ölü bant**, **anti-windup**, **çıkış doyumu** ve **slewrate** ile komutların fiziksel ve otopilot sınırlarına uyumu hedeflenir. **Telemetri füzyonu** ve **innovation tabanlı yumuşatma**, özellikle **kilit jitter** ve kısa süreli sensör sapmalarında üst seviye kararlılığı artırır.

**Yatış, dikilme ve yönelme** açısından bu workspace’in üst katmanı **hız ve yaw hızı setpoint** üretimine odaklanır; **roll/pitch iç döngüleri** tipik olarak **Alacakart** gibi gömülü otopilot üzerinde kalır. Böylece **görüş hatası → üst seviye kinematik komut → alt seviye attitude kontrolü** ayrımı korunur. İlave olarak **kaçış kontrolcüsü**, kilit kaybı veya tehdit sinyallerinde **güvenlik manevralarını** destekleyerek sistemin **dayanıklılık** boyutunu tamamlar.

| Yöntem | Bu projede durum | Avantajlar | Dezavantajlar |
|--------|------------------|------------|---------------|
| **PID (çok eksen, slewrate + anti-windup)** | Üst seviye rehberlikte kullanılıyor | Düşük hesaplama; sahada ayarlanabilir | Güçlü doğrusal olmayanlıkta tek başına sınırlı |
| **Saf P veya doğrudan oransal yaw/pitch** | Tanımlı birincil yöntem değil | Basit | Salınım ve gürültüye duyarlı |
| **LQR / H∞** | Üst seviye hatada doğrudan yok | Model tabanlı tasarım imkânı | Doğru uçak modeli ve entegrasyon maliyeti |
| **MPC** | Üst seviye hatada doğrudan yok | Kısıt yönetimi | Edge gerçek zamanlı maliyet ve bakım |
| **Pure pursuit / L1 (yol izleme)** | Ana görüş rehberliği değil | Coğrafi yol için uygun | Burada birincil girdi görüş hata vektörü |

**[Buraya PID/Kontrolcü Şeması (hata girişleri → eksen PID → slewrate/doyum → setpoint çıkışı) Eklenecek]**  
**[Buraya Telemetri / Görüntü Hızı Füzyonu Basit Blok Diyagramı Eklenecek]**
