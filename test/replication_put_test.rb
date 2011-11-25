require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class ReplicationPutTest < Verm::TestCase
  def assert_wrong_path
    yield
    fail "Expected a 403 Forbidden error"
  rescue Net::HTTPServerException => e
    assert e.response.is_a?(Net::HTTPForbidden)
  end

  def test_saves_files_under_requested_path
    put_file :path => '/foo/lO/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'

    put_file :path => '/foo/iX/jUKZYPw2NppTMTYyev5t3_x9NhyB-MLfbv1pDDGX0A',
             :file => 'another_text_file',
             :type => 'application/octet-stream'

    put_file :path => '/different/lO/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'
  end

  def test_saves_binary_files_without_truncation_or_miscoding
    put_file :path => '/foo/QK/_y6dLYki5Hr9RkjmlnSXFYeF-9Hahw5xECZr-USIAA',
             :file => 'binary_file',
             :type => 'application/octet-stream'
  end
  
  def test_saves_unknown_type_files_without_complaining
    put_file :path => '/foo/QK/_y6dLYki5Hr9RkjmlnSXFYeF-9Hahw5xECZr-USIAA',
             :file => 'binary_file',
             :type => 'unknown/type'
  end

  def test_saves_text_plain_files_with_suitable_extension
    put_file :path => '/foo/lO/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA.txt',
             :file => 'simple_text_file',
             :type => 'text/plain'
  end

  def test_saves_text_html_files_with_suitable_extension
    put_file :path => '/foo/lO/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA.html',
             :file => 'simple_text_file',
             :type => 'text/html'
  end

  def test_saves_image_jpeg_files_with_suitable_extension
    put_file :path => '/foo/Pq/zxC_V5Ck2n4fpzkhcYTETB5fitgmUgHgiqUCh-Ns0A.jpg',
             :file => 'jpeg',
             :type => 'image/jpeg'
  end

  def test_saves_gzip_content_encoded_files_as_gzipped_but_returns_non_gzipped_path
    put_file :path => '/foo/lO/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA.txt',
             :file => 'simple_text_file.gz',
             :type => 'text/plain',
             :encoding => 'gzip',
             :expected_extension_suffix => 'gz' # this is further expected on the filename
  end

  def test_no_save_files_under_wrong_path
    assert_wrong_path do
      put_file :path => '/foo/lO/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AB',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/l0/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/lO_SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/l1/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    assert_wrong_path do
      put_file :path => '/foo/l0/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AAb',
               :file => 'simple_text_file',
               :type => 'application/octet-stream'
    end

    put_file :path => '/foo/lO/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'

    put_file :path => '/different/path/lO/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'

    put_file :path => '/foo/lO/SLenl_se3zbBC0n88buiy2vAadjA9lq2xXRPccE7AA.ext',
             :file => 'simple_text_file',
             :type => 'application/octet-stream'
  end
end
